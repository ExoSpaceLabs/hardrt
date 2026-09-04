/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt.h"
#include "hardrt_time.h"
#include "hardrt_port_contract.h"

/* Null port: provides stub hooks so the library links.
   - No tick source is started.
   - No context switching happens.
   - Useful only to compile and link the core on bare toolchains.
*/

void hrt__init_idle_task(void) {
    _hrt_tcb_t *idle = hrt__tcb(HRT_IDLE_ID);
    if (idle == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
    idle->state = HRT_READY;
    idle->prio = 0u;
    idle->timeslice_cfg = 0u;
    idle->slice_left = 0u;
}

void hrt_port_enter_scheduler(void) {
    /* No scheduler in null port; hrt_start() returns immediately. */
}

void hrt_port_start_systick(const uint32_t tick_hz) {
    (void)tick_hz;
}

void hrt_port_idle_wait(void) {
}

void hrt__pend_context_switch(void) {
}

void hrt_port_yield_to_scheduler(void) {
}

void hrt__task_trampoline(void) {
    const int id = hrt__get_current();
    const _hrt_tcb_t *t = hrt__tcb(id);
    if (t && t->entry) t->entry(t->arg);
    hrt_task_delete();
}

void hrt_port_prepare_task_stack(const int id, void (*tramp)(void),
                                 uint32_t *stack_base, const size_t words) {
    (void)id;
    (void)tramp;
    (void)stack_base;
    (void)words;
}

void hrt_port_crit_enter(void) {
}

void hrt_port_crit_exit(void) {
}

void hrt_port_sp_valid(const uintptr_t sp) {
    (void)sp;
}
