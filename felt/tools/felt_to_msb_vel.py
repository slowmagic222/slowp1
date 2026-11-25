#!/usr/bin/env python3
"""Turn FELT v16's bank (felt-bank.bin, "FLT5") and its C tables into one MSB1 bank file for ms_core.

usage: felt_to_msb.py <felt-keyboard-v16 dir> <felt-bank.bin> <out.msb> [name]

Reads from the v16 sources: felt/bank.h (zones), felt/loop_metadata.h (loops, peaks), felt/felt_makeup.h and
felt/source_makeup.h (gains). The audio is copied unchanged. The layout is in bank-format.md.
"""
import re
import struct
import sys
import zlib


def numbers(text):
    return [int(x) for x in re.findall(r"-?\d+", text)]


def table(src, name):
    m = re.search(r"\b" + re.escape(name) + r"\b[^=]*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit("missing table " + name)
    return numbers(m.group(1))


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    root, bank_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
    name = (sys.argv[4] if len(sys.argv) > 4 else "felt").encode()[:16]
    rd = lambda f: open(f"{root}/felt/{f}", encoding="utf-8").read()
    bank_h, loops_h = rd("bank.h"), rd("loop_metadata.h")
    makeup_h, source_h = rd("felt_makeup.h"), rd("source_makeup.h")

    zones = table(bank_h, "ms_samples")
    count = len(zones) // 7
    assert count * 7 == len(zones)
    loop_start = table(loops_h, "ms_loop_start")[:count]
    loop_end = table(loops_h, "ms_loop_end")[:count]
    loop_limit = table(loops_h, "ms_loop_limit")[:count]
    loop_enabled = table(loops_h, "ms_loop_enabled")[:count]
    peaks = table(loops_h, "ms_peaks")
    rate = int(re.search(r"#define MS_RATE (\d+)", loops_h).group(1))
    felt_makeup = table(makeup_h, "ms_felt_makeup")
    source_makeup = table(source_h, "ms_source_makeup")
    note_inc = table(bank_h, "ms_note_inc")
    assert len(felt_makeup) == count * 5 and len(source_makeup) == 128 * 3 and len(peaks) == count * 64

    raw = open(bank_path, "rb").read()
    magic, payload_bytes, crc, file_rate = struct.unpack("<4sIII", raw[:16])
    assert magic == b"FLT5" and file_rate == rate, (magic, file_rate)
    audio = raw[16:16 + payload_bytes]
    assert len(audio) == payload_bytes and zlib.crc32(audio) == crc, "FLT5 payload CRC"

    # zone list: (source zone index, low, high, take, vel_low, vel_high, makeup scale)
    plan = []
    for i in range(count):
        offset, frames, root_inc, root, low, high, take = zones[i * 7:i * 7 + 7]
        assert root_inc == note_inc[root], "root_inc must be the root note's own step"
        same = sum(1 for k in range(count) if zones[k * 7 + 4] == low)   # the region's takes
        # velocity layers: take 2 is the firmer recording (vl2), heard only from velocity 96; takes 1 and 3 (vl1 and a
        # neighbour's vl1, retuned) are the soft ones, in turn below 96; a region of one take answers every velocity
        vlo, vhi = (0, 127) if same == 1 else ((96, 127) if take == 2 else (0, 95))
        scale = 1.0
        if low == 59:
            # B..F (59-65): both D3 recordings have a hammer attack (peak about 13x the body, about 3x elsewhere),
            # so both play only from velocity 96; the soft layer is the clean B2 take and the clean F3 take
            if take == 1:
                continue            # D3 vl1: the harshest hammer and overtones (by ear), left out
            if take == 2:
                vlo, vhi = 96, 127
            else:
                scale = 0.77        # B2 body 2223 and F3 body 1329 meet near 1700
        plan.append((i, low, high, take, vlo, vhi, scale))
    f3 = [i for i in range(count) if zones[i * 7 + 3] == 65 and zones[i * 7 + 4] == 66]
    assert len(f3) == 1
    plan.append((f3[0], 59, 65, 4, 0, 95, 1.28))

    zone_bytes = b""
    peak_list = []
    for i, low, high, take, vlo, vhi, scale in plan:
        offset, frames, root_inc, root = zones[i * 7:i * 7 + 4]
        mk = [min(65535, int(round(m * scale))) for m in felt_makeup[i * 5:i * 5 + 5]]
        rec = struct.pack("<5I8B5HH", offset, frames, loop_start[i], loop_end[i], loop_limit[i],
                          root, low, high, take, vlo, vhi, loop_enabled[i], 128, *mk, 4096)
        zone_bytes += rec + bytes(48 - len(rec))
        peak_list += peaks[i * 64:i * 64 + 64]
    peaks = peak_list
    count = len(plan)

    header_size = 64
    zones_at = header_size
    makeup_at = zones_at + len(zone_bytes)
    makeup = struct.pack("<%dH" % len(source_makeup), *source_makeup)
    peaks_at = makeup_at + len(makeup)
    peak_bytes = struct.pack("<%dh" % len(peaks), *peaks)
    audio_at = peaks_at + len(peak_bytes)
    audio_at = (audio_at + 3) & ~3
    body = zone_bytes + makeup + peak_bytes
    body += bytes(audio_at - header_size - len(body)) + audio
    total = header_size + len(body)
    head = struct.pack("<4sHHIIIHHIIIIIHH16s", b"MSB1", 1, header_size, total, zlib.crc32(body), rate,
                       count, 48, zones_at, makeup_at, peaks_at, audio_at, len(audio), 0, 0, name.ljust(16, b"\0"))
    assert len(head) == header_size
    open(out, "wb").write(head + body)
    print(f"{out}: {total} bytes, {count} zones, {rate} Hz, audio {len(audio)} bytes at {audio_at}")


if __name__ == "__main__":
    main()
