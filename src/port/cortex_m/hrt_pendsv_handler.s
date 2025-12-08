
.extern hrt__schedule

.syntax unified
.thumb
.global PendSV_Handler
.type   PendSV_Handler, %function

/*
 * PendSV_Handler - Cortex-M context switch handler for HeartOS
 *
 * Contract with C side:
 *   - hrt__save_current_sp(uint32_t *sp):
 *       Saves the updated PSP (after pushing r4-r11) into the current TCB.
 *
 *   - int hrt__pick_next_ready(void):
 *       Returns the next task id to run.
 *
 *   - uint32_t *hrt__load_next_sp_and_set_current(int id):
 *       Sets the current task id and returns that task's saved SP.
 *
 * Stack layout expected when switching OUT of a running task:
 *   [high addr]
 *       r4
 *       r5
 *       r6
 *       r7
 *       r8
 *       r9
 *       r10
 *       r11        <-- value stored in TCB->sp
 *       r0         \
 *       r1          \
 *       r2           \
 *       r3            \
 *       r12            > hardware-saved frame
 *       lr            /
 *       pc           /
 *       xpsr        /
 *   [low addr]
 */

PendSV_Handler:
    cpsid   i

    mrs     r0, psp          @ r0 = old PSP, or 0 on first switch?
    cbz     r0, first_switch @ if PSP == 0, we haven't started any task yet

normal_switch:
    stmdb   r0!, {r4-r11}    @ save callee-saved regs on current stack
    bl      hrt__schedule    @ r0 = old_sp, returns new_sp (or 0)
    cbz     r0, resume       @ no runnable task -> don't change context

    ldmia   r0!, {r4-r11}
    msr     psp, r0

    ldr     r1, =0xFFFFFFFD  @ return to Thread mode, use PSP
    mov     lr, r1
    cpsie   i
    bx      lr

first_switch:
    movs    r0, #0           @ old_sp = 0 for first entry
    bl      hrt__schedule    @ just pick first task, no save
    cbz     r0, resume

    ldmia   r0!, {r4-r11}
    msr     psp, r0

    ldr     r1, =0xFFFFFFFD
    mov     lr, r1
    cpsie   i
    bx      lr

resume:
    cpsie   i
    bx      lr

    .size PendSV_Handler, .-PendSV_Handler