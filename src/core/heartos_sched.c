/* SPDX-License-Identifier: Apache-2.0 */
#include "heartos.h"
#include "heartos_time.h"

/* Core-private accessors from heartos_core.c */
int   hrt__pick_next_ready(void);
void  hrt__make_ready(int id);
void  hrt__requeue_noreset(int id);
int   hrt__get_current(void);
void  hrt__set_current(int id);
void  hrt__inc_tick(void);
void  hrt__pend_context_switch(void);
hrt_policy_t hrt__policy(void);

/* Mirror of TCB; must match heartos_core.c */
typedef struct {
    uint32_t *sp;
    uint32_t *stack_base;
    size_t    stack_words;
    hrt_task_fn entry;
    void*     arg;
    uint32_t  wake_tick;
    uint16_t  timeslice_cfg;
    uint16_t  slice_left;
    uint8_t   prio;
    uint8_t   state; /* HRT_READY/HRT_SLEEP/... */
} hrt_tcb_t;

hrt_tcb_t* hrt__tcb(int id);

void hrt__tick_isr(void){
    /* advance time */
    hrt__inc_tick();

    /* wake sleepers */
    for (int i = 0; i < HEARTOS_MAX_TASKS; ++i) {
        hrt_tcb_t* t = hrt__tcb(i);
        if (!t) continue;
        if (t->state == HRT_SLEEP) {
            /* signed compare handles wrap */
            if ((int32_t)(t->wake_tick - hrt_tick_now()) <= 0) {
                hrt__make_ready(i);
            }
        }
    }

    /* RR time-slice accounting for the currently running task.
       Note: Ports must not context-switch from ISR; we only pend a switch. */
    int cur = hrt__get_current();
    if (cur >= 0) {
        hrt_tcb_t* ct = hrt__tcb(cur);
        if (ct && ct->state == HRT_READY) {
            hrt_policy_t pol = hrt__policy();
            if ((pol == HRT_SCHED_RR || pol == HRT_SCHED_PRIORITY_RR) && ct->timeslice_cfg > 0) {
                if (ct->slice_left > 0) {
                    ct->slice_left--;
                    if (ct->slice_left == 0) {
                        /* Time slice expired: request reschedule. The actual
                           requeue happens when the task yields/sleeps (safe ctx). */
                        hrt__pend_context_switch();
                    }
                }
            }
        }
    }

    /* Also wake-driven changes may require a reschedule; ports decide when to switch */
    hrt__pend_context_switch();
}
