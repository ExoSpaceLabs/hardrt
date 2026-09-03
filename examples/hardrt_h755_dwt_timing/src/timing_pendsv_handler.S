/* SPDX-License-Identifier: Apache-2.0 */

.syntax unified
.thumb

.extern hrt__schedule
.extern g_pendsv_measure_armed
.extern g_pendsv_sample_ready
.extern g_pendsv_entry_cycles
.extern g_pendsv_save_cycles
.extern g_pendsv_scheduler_cycles
.extern g_pendsv_restore_cycles
.extern g_pendsv_software_cycles

.global PendSV_Handler
.type PendSV_Handler, %function

/*
 * Diagnostic-only PendSV handler used by the scheduler_decision timing image.
 * It mirrors the production Cortex-M handler while recording DWT cycle deltas
 * for the one switch armed by the TIM2 ISR. The production HardRT handler is
 * not linked into this image because this definition satisfies the vector.
 *
 * Measured software segments:
 *   entry -> outgoing r4-r11 saved
 *   direct hrt__schedule() call
 *   scheduler return -> incoming r4-r11 + PSP restored
 *   entry -> incoming context restored
 *
 * The awakened task separately records entry -> task continuation after the
 * blocked semaphore API returns.
 */
PendSV_Handler:
    cpsid   i

    /* If this is the armed TIM2 wake, capture software-handler entry in r12.
       r12 is hardware-stacked on exception entry and can be used as scratch. */
    ldr     r1, =g_pendsv_measure_armed
    ldr     r2, [r1]
    cbz     r2, 1f
    ldr     r3, =0xE0001004
    ldr     r12, [r3]
1:
    mrs     r0, psp
    cbz     r0, first_switch

    stmdb   r0!, {r4-r11}

    ldr     r1, =g_pendsv_measure_armed
    ldr     r2, [r1]
    cbz     r2, unmeasured_schedule

    /* Save segment: DWT read immediately after the outgoing context store. */
    ldr     r3, =0xE0001004
    ldr     r2, [r3]
    subs    r2, r2, r12
    ldr     r1, =g_pendsv_save_cycles
    str     r2, [r1]

    /* Preserve handler-entry timestamp across the C call in callee-saved r10.
       Outgoing r10/r11 are already stored, so both registers are scratch here. */
    mov     r10, r12
    ldr     r11, [r3]

    bl      hrt__schedule

    /* Scheduler segment: direct call, no C wrapper layer. */
    ldr     r3, =0xE0001004
    ldr     r2, [r3]
    subs    r2, r2, r11
    ldr     r1, =g_pendsv_scheduler_cycles
    str     r2, [r1]

    cbz     r0, measured_resume

    /* Restore segment starts after scheduler bookkeeping and before ldmia. */
    mov     r12, r10
    ldr     r3, =0xE0001004
    ldr     r1, [r3]

    ldmia   r0!, {r4-r11}
    msr     psp, r0

    ldr     r2, [r3]
    subs    r1, r2, r1
    ldr     r3, =g_pendsv_restore_cycles
    str     r1, [r3]

    /* Same end timestamp gives software-handler entry -> restored context. */
    subs    r1, r2, r12
    ldr     r3, =g_pendsv_software_cycles
    str     r1, [r3]

    ldr     r3, =g_pendsv_entry_cycles
    str     r12, [r3]

    movs    r2, #1
    ldr     r3, =g_pendsv_sample_ready
    str     r2, [r3]

    movs    r2, #0
    ldr     r3, =g_pendsv_measure_armed
    str     r2, [r3]

    ldr     r1, =0xFFFFFFFD
    mov     lr, r1
    cpsie   i
    bx      lr

unmeasured_schedule:
    bl      hrt__schedule
    cbz     r0, resume

    ldmia   r0!, {r4-r11}
    msr     psp, r0

    ldr     r1, =0xFFFFFFFD
    mov     lr, r1
    cpsie   i
    bx      lr

first_switch:
    movs    r0, #0
    bl      hrt__schedule
    cbz     r0, resume

    ldmia   r0!, {r4-r11}
    msr     psp, r0

    ldr     r1, =0xFFFFFFFD
    mov     lr, r1
    cpsie   i
    bx      lr

measured_resume:
    /* The armed event must have a runnable target. Disarm defensively if the
       scheduler ever returns 0 so a later unrelated switch is not sampled. */
    movs    r2, #0
    ldr     r3, =g_pendsv_measure_armed
    str     r2, [r3]

resume:
    cpsie   i
    bx      lr

.size PendSV_Handler, .-PendSV_Handler
