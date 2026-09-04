/* SPDX-License-Identifier: Apache-2.0 */

.syntax unified
.thumb
.fpu fpv5-d16

.extern hrt_yield

/* Validation-only helper.
 *
 * C signature:
 *   void hrt_fp_roundtrip_yield(const uint32_t *bank_in,
 *                               uint32_t fpscr_in,
 *                               uint32_t *bank_out,
 *                               uint32_t *fpscr_out);
 *
 * It intentionally seeds the complete FP register bank plus FPSCR, requests a
 * real HardRT yield, and snapshots the state immediately after this task
 * resumes. The helper deliberately violates the normal AAPCS rule that
 * s16-s31 are callee-saved: that violation is the object under test. */
.global hrt_fp_roundtrip_yield
.type hrt_fp_roundtrip_yield, %function
hrt_fp_roundtrip_yield:
    push    {r4-r6, lr}
    mov     r4, r2
    mov     r5, r3
    vldmia  r0!, {s0-s15}
    vldmia  r0!, {s16-s31}
    vmsr    fpscr, r1
    bl      hrt_yield
    vstmia  r4!, {s0-s15}
    vstmia  r4!, {s16-s31}
    vmrs    r6, fpscr
    str     r6, [r5]
    pop     {r4-r6, pc}
.size hrt_fp_roundtrip_yield, .-hrt_fp_roundtrip_yield
