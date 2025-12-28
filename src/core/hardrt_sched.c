/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt.h"
#include "hardrt_port_int.h"
#include "hardrt_time.h"

#ifndef HARDRT_DEBUG
    #define HARDRT_BDG_VARIABLES 0
    #define HARDRT_VALIDATION 0
#else
    #define HARDRT_BDG_VARIABLES 1
    #define HARDRT_VALIDATION 1
#endif

/* Core-private accessors from hardrt_core.c */

#if HARDRT_BDG_VARIABLES == 1
volatile uint32_t dbg_pend_from_tick  = 0;
volatile uint32_t ipsr;
#endif
void hrt__make_ready(int id);

void hrt__requeue_noreset(int id);

void hrt__set_current(int id);

void hrt__inc_tick(void);

hrt_policy_t hrt__policy(void);

void hrt__pend_context_switch(void);

void hrt__tick_isr(void) {
    /* advance time */
    hrt__inc_tick();

    uint8_t triggerPendSV = 0;
    /* wake sleepers */
    for (int i = 0; i < HARDRT_MAX_TASKS; ++i) {
        _hrt_tcb_t *t = hrt__tcb(i);
        if (!t) continue;
        if (t->state == HRT_SLEEP) {
            /* signed compare handles wrap */
            if ((int32_t) (t->wake_tick - hrt_tick_now()) <= 0) {
                hrt__make_ready(i);
                triggerPendSV = 1;
            }
        }
    }

    /* RR time-slice accounting for the currently running task.
       Note: Ports must not context-switch from ISR; we only pend a switch. */
    const int cur = hrt__get_current();
    if (cur < 0 || cur >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
    }else {
        _hrt_tcb_t *ct = hrt__tcb(cur);
        if (!ct) hrt_error(ERR_TCB_NULL);
        if (ct && ct->state == HRT_READY) {
            const hrt_policy_t pol = hrt__policy();
            if ((pol == HRT_SCHED_RR || pol == HRT_SCHED_PRIORITY_RR) && ct->timeslice_cfg > 0) {
                if (ct->slice_left > 0) {
                    ct->slice_left--;
                    if (ct->slice_left == 0) {
                        /* Time slice expired: request rescheduling. The actual
                           requeue happens when the task yields/sleeps (safe ctx). */

                        triggerPendSV = 1;
                    }
                }
            }
        }
    }

    /* Also, wake-driven changes may require a re-schedule; ports decide when to switch */
    if (triggerPendSV != 0) {
#if HARDRT_BDG_VARIABLES == 1
        dbg_pend_from_tick++;
#endif
        hrt__pend_context_switch();
    }
}

void hrt_tick_from_isr(void) {
    // Only allow tick advancement when using EXTERNAL mode
#if HARDRT_BDG_VARIABLES
    __asm volatile ("mrs %0, ipsr" : "=r"(ipsr));
    (void)ipsr;
#endif

    if (hrt__cfg_tick_src() != HRT_TICK_EXTERNAL) {
        // Defensive: ignore if called while the port owns SysTick
        // Optional: log or blink an LED if you want debug visibility
        //Todo Set global variable possibly hardrt status code
        // for when the system exits it can be checked for error code?
        return;
    }
    hrt__tick_isr();
}
