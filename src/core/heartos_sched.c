/* SPDX-License-Identifier: Apache-2.0 */
#include "heartos.h"
#include "heartos_time.h"

/* Core-private accessors from heartos_core.c */
int   hrt__pick_next_ready(void);
void  hrt__make_ready(int id);
int   hrt__get_current(void);
void  hrt__set_current(int id);
void  hrt__inc_tick(void);
void  hrt__pend_context_switch(void);

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

    /* Safe to request a rescheduling; ports decide when to actually switch */
    hrt__pend_context_switch();
}
