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

#ifndef HARDRT_NVIC_PRIO_BITS
#define HARDRT_NVIC_PRIO_BITS 4
#endif

#ifndef HARDRT_MAX_SYSCALL_IRQ_PRIO
#define HARDRT_MAX_SYSCALL_IRQ_PRIO 5
#endif

#define HRT_PENDSV_BASEPRI ((HARDRT_MAX_SYSCALL_IRQ_PRIO << (8 - HARDRT_NVIC_PRIO_BITS)) & 0xFF)

.global PendSV_Handler
.type PendSV_Handler, %function

/*
 * Diagnostic-only PendSV handler used by the scheduler_decision timing image.
 * It mirrors the production Cortex-M handler while recording DWT cycle deltas
 * for the one switch armed by the TIM2 ISR. The production HardRT handler is
 * not linked into this image because this definition satisfies the vector.
 *
 * On hardware-FP builds this mirror also preserves task EXC_RETURN and saves
 * s16-s31 only for an extended FP context, matching production semantics.
 * PendSV uses the same BASEPRI ceiling as production rather than PRIMASK, so
 * the timing image measures the actual v0.5 interrupt-mask contract.
 */
PendSV_Handler:
    /* Capture software-handler entry before the BASEPRI prologue. r12 is
       hardware-stacked and therefore safe scratch in Handler mode. */
    ldr     r3, =0xE0001004
    ldr     r12, [r3]

    mrs     r2, BASEPRI
    mov     r3, lr
    movs    r1, #HRT_PENDSV_BASEPRI
    msr     BASEPRI_MAX, r1
    dsb     0xF
    isb     0xF
    push    {r2, r3}

    mrs     r0, psp
    cmp     r0, #0
    beq.w   first_switch

#if defined(__ARM_FP) && (__ARM_FP != 0)
    tst     lr, #0x10
    it      eq
    vstmdbeq r0!, {s16-s31}
    stmdb   r0!, {r4-r11, lr}
#else
    stmdb   r0!, {r4-r11}
#endif

    ldr     r1, =g_pendsv_measure_armed
    ldr     r2, [r1]
    cmp     r2, #0
    beq.w   unmeasured_schedule

    /* Save segment: handler entry through outgoing context store, including
       the production BASEPRI prologue and MSP bookkeeping. */
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

    cmp     r0, #0
    beq.w   measured_resume

    /* Restore segment starts after scheduler bookkeeping. */
    mov     r12, r10
    ldr     r3, =0xE0001004
    ldr     r1, [r3]

#if defined(__ARM_FP) && (__ARM_FP != 0)
    ldmia   r0!, {r4-r11, lr}
    tst     lr, #0x10
    it      eq
    vldmiaeq r0!, {s16-s31}
#else
    ldmia   r0!, {r4-r11}
    ldr     r2, =0xFFFFFFFD
    mov     lr, r2
#endif
    msr     psp, r0

    pop     {r2, r3}
    msr     BASEPRI, r2
    dsb     0xF
    isb     0xF

    ldr     r3, =0xE0001004
    ldr     r2, [r3]
    subs    r1, r2, r1
    ldr     r3, =g_pendsv_restore_cycles
    str     r1, [r3]

    /* Same end timestamp gives software-handler entry -> restored context and
       exact pre-entry BASEPRI state. */
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

    bx      lr

unmeasured_schedule:
    bl      hrt__schedule
    cmp     r0, #0
    beq.w   resume

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

    pop     {r2, r3}
    msr     BASEPRI, r2
    dsb     0xF
    isb     0xF
    bx      lr

first_switch:
    movs    r0, #0
    bl      hrt__schedule
    cmp     r0, #0
    beq.w   resume

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

    pop     {r2, r3}
    msr     BASEPRI, r2
    dsb     0xF
    isb     0xF
    bx      lr

measured_resume:
    /* The armed event must have a runnable target. Disarm defensively if the
       scheduler ever returns 0 so a later unrelated switch is not sampled. */
    movs    r2, #0
    ldr     r3, =g_pendsv_measure_armed
    str     r2, [r3]

resume:
    pop     {r2, r3}
    mov     lr, r3
    msr     BASEPRI, r2
    dsb     0xF
    isb     0xF
    bx      lr

.size PendSV_Handler, .-PendSV_Handler
