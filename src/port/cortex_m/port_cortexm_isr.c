/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>   // for size_t
#include "heartos_port_int.h"


/* Core-private hooks */
int hrt__pick_next_ready(void);

int hrt__get_current(void);

void hrt__set_current(int id);

/* Mirror TCB, as in port_cortexm.c */
typedef struct {
    uint32_t *sp;
    uint32_t *stack_base;
    size_t stack_words;

    void (*entry)(void *);

    void *arg;
    uint32_t wake_tick;
    uint16_t timeslice_cfg;
    uint16_t slice_left;
    uint8_t prio;
    uint8_t state;
} _hrt_tcb_t;

extern _hrt_tcb_t *hrt__tcb(int id);

/* Helper to fetch/store SP for a given task id */
static inline uint32_t *_get_sp(int id) { return hrt__tcb(id)->sp; }
static inline void _set_sp(int id, uint32_t *sp) { hrt__tcb(id)->sp = sp; }

/* PendSV does the actual context switch. */
__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile(
        /* Use PSP (process stack pointer) */
        "mrs r0, psp                         \n"
        /* If there is a current task, save r4-r11 onto its stack and write PSP to TCB */
        "ldr r3, =hrt__get_current          \n"
        "blx r3                              \n" /* r0 = current id */
        "cmp r0, #0                          \n"
        "blt 1f                              \n" /* if <0, no current task yet */
        /* save r4-r11 */
        "mrs r1, psp                         \n"
        "stmdb r1!, {r4-r11}                 \n"
        /* store updated PSP into TCB->sp */
        "mov r2, r0                          \n"
        "ldr r3, =hrt__tcb                   \n"
        "blx r3                              \n" /* r0 = TCB* */
        "str r1, [r0]                        \n" /* tcb->sp = r1 */
        "1:                                  \n"

        /* Pick next ready task */
        "ldr r3, =hrt__pick_next_ready       \n"
        "blx r3                              \n" /* r0 = next id or -1 */

        /* If none ready, just return (idle will WFI) */
        "cmp r0, #0                          \n"
        "blt 2f                              \n"

        /* Set current = next */
        "mov r4, r0                          \n"
        "ldr r3, =hrt__set_current           \n"
        "blx r3                              \n"

        /* Load next task PSP from its TCB */
        "mov r0, r4                          \n"
        "ldr r3, =hrt__tcb                   \n"
        "blx r3                              \n" /* r0 = TCB* */
        "ldr r1, [r0]                        \n" /* r1 = tcb->sp */
        /* restore r4-r11 from next stack */
        "ldmia r1!, {r4-r11}                 \n"
        "msr psp, r1                         \n"

        /* return to thread mode, continue with next task */
        "bx lr                               \n"

        "2: bx lr                            \n"
    );
}

/* The trampoline runs with the task's PSP already loaded. It must:
 * - grab the TCB's entry and arg
 * - call entry(arg)
 * - when it returns, just pend a switch forever
 */
extern void hrt__pend_context_switch(void);

void hrt__task_trampoline(void) {
    int id = hrt__get_current();
    _hrt_tcb_t *t = hrt__tcb(id);

    /* r0 arg is ignored on entry; call real entry now */
    t->entry(t->arg);

    /* If task returns, yield forever */
    for (;;) {
        hrt__pend_context_switch();
        __asm volatile ("wfi");
    }
}
