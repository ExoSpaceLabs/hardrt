/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>   // for size_t
#include "hardrt.h"
#include "hardrt_port_int.h"

#ifndef HARDRT_DEBUG
    #define HARDRT_BDG_VARIABLES 0
    #define HARDRT_VALIDATION 0
#else
    #define HARDRT_BDG_VARIABLES 1
    #define HARDRT_VALIDATION 1
#endif

/* Core-private hooks */
#if HARDRT_BDG_VARIABLES == 1
volatile uint8_t dbg_tasks_returned = 0;
volatile uint8_t dbg_pend_from_tramp = 0;
#endif
extern void hrt__pend_context_switch(void);
extern _hrt_tcb_t *hrt__tcb(int id);

/* The trampoline runs with the task's PSP already loaded. It must:
 * - grab the TCB's entry and arg
 * - call entry(arg)
 * - when it returns, just pend a switch forever
 */

/* PendSV does the actual context switch. defined in hrt_pendsv_handler.s */
void PendSV_Handler(void);

void hrt__task_trampoline(void) {
    int id = hrt__get_current();

#if HARDRT_VALIDATION == 1
    if (id < 0) {
        hrt_error(ERR_INVALID_ID);
    }
#endif
    _hrt_tcb_t *t = hrt__tcb(id);
#if HARDRT_VALIDATION == 1
    if (t == NULL) {
        hrt_error(ERR_TCB_NULL);
    }
#endif

    /* r0 arg is ignored on entry; call real entry now */
    t->entry(t->arg);
#if HARDRT_BDG_VARIABLES == 1
    dbg_tasks_returned++;
    (void)dbg_tasks_returned;
#endif
    /* If task returns, yield forever */
    for (;;) {
#if HARDRT_BDG_VARIABLES == 1
        dbg_pend_from_tramp++;
#endif
        hrt__pend_context_switch();
        __asm volatile ("wfi");
    }
}
