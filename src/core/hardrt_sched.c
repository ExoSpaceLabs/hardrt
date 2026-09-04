/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt.h"
#include "hardrt_port_int.h"
#include "hardrt_time.h"

#ifndef HARDRT_DEBUG
#define HARDRT_DEBUG 0
#endif

#if HARDRT_DEBUG == 1
volatile uint32_t dbg_pend_from_tick = 0;
volatile uint32_t ipsr;
#endif

void hrt__tick_isr(void) {
    hrt__inc_tick();
    uint8_t trigger_pendsv = (uint8_t)(hrt__sleep_tick() != 0);

    const int cur = hrt__get_current();
    if (cur < 0 || cur >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
    } else {
        _hrt_tcb_t *ct = hrt__tcb(cur);
        if (!ct) hrt_error(ERR_TCB_NULL);
        if (ct && ct->state == HRT_READY) {
            const hrt_policy_t pol = hrt__policy();
            if ((pol == HRT_SCHED_RR || pol == HRT_SCHED_PRIORITY_RR) &&
                ct->timeslice_cfg > 0u && ct->slice_left > 0u) {
                ct->slice_left--;
                if (ct->slice_left == 0u) trigger_pendsv = 1u;
            }
        }
    }

    if (trigger_pendsv != 0u) {
#if HARDRT_DEBUG == 1
        dbg_pend_from_tick++;
#endif
        hrt__pend_context_switch();
    }
}

void hrt_tick_from_isr(void) {
    if (hrt__cfg_tick_src() != HRT_TICK_EXTERNAL) {
        hrt_error(ERR_TICK_SOURCE_MISMATCH);
        return;
    }
    hrt__tick_isr();
}
