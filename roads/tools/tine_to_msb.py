#!/usr/bin/env python3
"""Turn TINE 0.2's bank (tine-bank.bin, "TIN1") and its C tables into one MSB1 bank file for ms_core (roads.pack).

usage: tine_to_msb.py <tine-rhodes-v2 dir> <tine-bank.bin> <out.msb> [--velocity] [name]

Reads tine/bank.h (zones), tine/loop_metadata.h (loops, natural decay, peaks), tine/source_makeup.h. The audio is
copied unchanged. Flags = 1 (MS_BANK_TINE: TINE's character). Zone bytes 40..43 hold TINE's natural decay (Q30 a
frame at 88.2 kHz).
--velocity: TINE's three recordings per region are three dynamics, so let velocity choose them (take 1 is the
hardest, 85..127; take 2, 43..84; take 3 the softest, 0..42: v4, heard on the unit, v3 had them the wrong way round)
instead of taking turns. Without it: the OG's round-robin. roads.pack can also ignore the ranges (its velocity switch).
"""
import re, struct, sys, zlib


def numbers(text):
    return [int(x) for x in re.findall(r"-?\d+", text)]


def table(src, name):
    m = re.search(r"\b" + re.escape(name) + r"\b[^=]*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit("missing table " + name)
    return numbers(m.group(1))


def main():
    args = [a for a in sys.argv[1:] if a != "--velocity"]
    velocity = "--velocity" in sys.argv
    if len(args) < 3:
        sys.exit(__doc__)
    root, bank_path, out = args[0], args[1], args[2]
    name = (args[3] if len(args) > 3 else "roads").encode()[:16]
    rd = lambda f: open(f"{root}/tine/{f}", encoding="utf-8").read()
    bank_h, loops_h, source_h = rd("bank.h"), rd("loop_metadata.h"), rd("source_makeup.h")

    zones = table(bank_h, "ms_samples")
    count = len(zones) // 7
    assert count * 7 == len(zones)
    loop_start = table(loops_h, "ms_loop_start")[:count]
    loop_end = table(loops_h, "ms_loop_end")[:count]
    loop_limit = table(loops_h, "ms_loop_limit")[:count]
    loop_enabled = table(loops_h, "ms_loop_enabled")[:count]
    decay = table(loops_h, "ms_loop_decay")[:count]
    peaks = table(loops_h, "ms_peaks")
    rate = int(re.search(r"#define MS_RATE (\d+)", loops_h).group(1))
    source_makeup = table(source_h, "ms_source_makeup")
    note_inc = table(bank_h, "ms_note_inc")
    assert len(source_makeup) == 128 * 3 and len(peaks) == count * 64 and len(decay) == count

    raw = open(bank_path, "rb").read()
    magic, payload_bytes, crc, file_rate = struct.unpack("<4sIII", raw[:16])
    assert magic == b"TIN1" and file_rate == rate, (magic, file_rate)
    audio = raw[16:16 + payload_bytes]
    assert len(audio) == payload_bytes and zlib.crc32(audio) == crc, "TIN1 payload CRC"

    layers = {1: (85, 127), 2: (43, 84), 3: (0, 42)}
    zone_bytes = b""
    for i in range(count):
        offset, frames, root_inc, root, low, high, take = zones[i * 7:i * 7 + 7]
        assert root_inc == note_inc[root], "root_inc must be the root note's own step"
        vlo, vhi = layers[take] if velocity else (0, 127)
        rec = struct.pack("<5I8B5HHI", offset, frames, loop_start[i], loop_end[i], loop_limit[i],
                          root, low, high, take, vlo, vhi, loop_enabled[i], 128,
                          4096, 4096, 4096, 4096, 4096, 4096, decay[i])
        zone_bytes += rec + bytes(48 - len(rec))

    header_size = 64
    zones_at = header_size
    makeup_at = zones_at + len(zone_bytes)
    makeup = struct.pack("<%dH" % len(source_makeup), *source_makeup)
    peaks_at = makeup_at + len(makeup)
    peak_bytes = struct.pack("<%dh" % len(peaks), *peaks)
    audio_at = (peaks_at + len(peak_bytes) + 3) & ~3
    body = zone_bytes + makeup + peak_bytes
    body += bytes(audio_at - header_size - len(body)) + audio
    total = header_size + len(body)
    head = struct.pack("<4sHHIIIHHIIIIIHH16s", b"MSB1", 1, header_size, total, zlib.crc32(body), rate,
                       count, 48, zones_at, makeup_at, peaks_at, audio_at, len(audio), 0, 1, name.ljust(16, b"\0"))
    assert len(head) == header_size
    open(out, "wb").write(head + body)
    print(f"{out}: {total} bytes, {count} zones, {rate} Hz, audio {len(audio)} bytes at {audio_at}, "
          f"{'velocity layers' if velocity else 'round-robin'}")


if __name__ == "__main__":
    main()
