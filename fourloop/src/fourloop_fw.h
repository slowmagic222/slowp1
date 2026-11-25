/* 4loop.pack: every place in the firmware the pack touches is in this file.
 * GPL-3.0-or-later + OP-1 firmware permission.
 *
 * To retarget to another OS version, change this file only. No pack code is patched into the
 * firmware's code: there is no table of changes and no CRC. The pack reads one data word, writes
 * the vtable word of one live object, and calls two of the firmware's heap functions. Everything
 * else goes through Faust's base.pack (op1f_* helpers), which carries its own addresses.
 *
 * OS 1.7.3+0 (stock os.ldr SHA-256 d134eb1a..., the test unit). Values from open-field's 1.7.3 sources
 * (github.com/lap0573/open-field, commit 468b132), checked against our own 1.7.3 file:
 *   - master producer slot / fx-active byte: module/build_h_fx.cpp lines 25-28, 68-69. Both lie in the
 *     zero-filled data block at 0x8491c7d4 of our os.ldr, and both moved by +0x440 from 1.7.0, as expected.
 *   - heap_malloc / heap_free: docs/firmware-interfaces.md, "Texts and memory"; both start with LINK 0
 *     (00 e8 00 00) in our os.ldr, i.e. real function entries.
 *   - master producer storage in L1 at 0x11906af8: unchanged from 1.7.0 (sanity check only).
 *   - process call shape vtable[2](self, int* L, int* R, ctx): build_h_chain.cpp, run_one().
 * 1.7.0 values, for the record: slot 0x84a03848, active 0x84a0112c, heap_malloc 0x8fcc3c48, heap_free 0x8fcc33dc.
 */
#ifndef FOURLOOP_FW_H
#define FOURLOOP_FW_H

#define FL_FW_NAME             "1.7.3"
#define FL_MASTER_SLOT         0x84a03c88u   /* u32: the master effect producer object, or 0 */
#define FL_MASTER_FX_ACTIVE    0x84a0156cu   /* u8: non-zero while the master effect runs */
#define FL_MASTER_STORAGE      0x11906af8u   /* where the factory builds the master producer (L1) */
#define FL_HEAP_MALLOC         0x8fcc5a68u   /* void* heap_malloc(int heap, u32 bytes): 0 when full. Not audio thread */
#define FL_HEAP_FREE           0x8fcc51fcu   /* u32 heap_free(int heap): bytes not in use */
#define FL_BLOCK               64            /* samples per audio block, per channel */

/* Address ranges used to sanity-check what we read before we write anything. */
#define FL_DDR_LO              0x80000000u
#define FL_DDR_HI              0x90000000u
#define FL_L1_DATA_LO          0x11800000u
#define FL_L1_DATA_HI          0x11a00000u
#define FL_L1_CODE_LO          0x11a00000u
#define FL_L1_CODE_HI          0x11a10000u

#endif
