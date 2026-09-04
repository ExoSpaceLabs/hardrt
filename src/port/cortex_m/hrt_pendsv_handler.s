/* SPDX-License-Identifier: Apache-2.0 */

.extern hrt__schedule

.syntax unified
.thumb
.global PendSV_Handler
.type   PendSV_Handler, %function

/*
 * PendSV_Handler - Cortex-M context switch handler for HardRT
 *
 * Basic task context:
 *   software: r4-r11
 *   hardware: r0-r3, r12, lr, pc, xPSR
 *
 * Hardware-FP task context:
 *   software: r4-r11, EXC_RETURN, and s16-s31 when EXC_RETURN[4] == 0
 *   hardware: s0-s15, FPSCR, reserved, r0-r3, r12, lr, pc, xPSR
 *
 * EXC_RETURN is saved only on hardware-FP builds because bit 4 selects the
 * basic versus extended exception frame. The FP high bank is therefore paid
 * only by tasks that actually own an FP context.
 */

PendSV_Handler:
    cpsid   i

    mrs     r0, psp
    cbz     r0, first_switch

normal_switch:
#if defined(__ARM_FP) && (__ARM_FP != 0)
    /* If Thread mode owns FP state, force/materialize lazy low-FP stacking as
       needed and preserve the callee-saved high FP bank below that frame. */
    tst     lr, #0x10
    it      eq
    vstmdbeq r0!, {s16-s31}
    stmdb   r0!, {r4-r11, lr}
#else
    stmdb   r0!, {r4-r11}
#endif

    bl      hrt__schedule
    cbz     r0, resume

#if defined(__ARM_FP) && (__ARM_FP != 0)
    ldmia   r0!, {r4-r11, lr}
    tst     lr, #0x10
    it      eq
    vldmiaeq r0!, {s16-s31}
#else
    ldmia   r0!, {r4-r11}
    ldr     r1, =0xFFFFFFFD
    mov     lr, r1
#endif
    msr     psp, r0

    cpsie   i
    bx      lr

first_switch:
    movs    r0, #0
    bl      hrt__schedule
    cbz     r0, resume

#if defined(__ARM_FP) && (__ARM_FP != 0)
    /* New tasks start with a basic PSP frame and saved EXC_RETURN=FFFFFFFD. */
    ldmia   r0!, {r4-r11, lr}
    tst     lr, #0x10
    it      eq
    vldmiaeq r0!, {s16-s31}
#else
    ldmia   r0!, {r4-r11}
    ldr     r1, =0xFFFFFFFD
    mov     lr, r1
#endif
    msr     psp, r0

    cpsie   i
    bx      lr

resume:
    cpsie   i
    bx      lr

    .size PendSV_Handler, .-PendSV_Handler
