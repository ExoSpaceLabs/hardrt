/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_event.h"
#include "hardrt_port_int.h"

#include <string.h>

#define HRT_EVENT_VALID_OPTIONS \
    ((unsigned)HRT_EVENT_WAIT_ALL | (unsigned)HRT_EVENT_CLEAR_ON_EXIT)

static int event_options_valid(const unsigned options) {
    return (options & ~HRT_EVENT_VALID_OPTIONS) == 0u;
}

static int event_matches(const hrt_event_bits_t bits,
                         const hrt_event_bits_t mask,
                         const unsigned options,
                         hrt_event_bits_t *matched) {
    const hrt_event_bits_t value = bits & mask;
    const int wait_all = (options & (unsigned)HRT_EVENT_WAIT_ALL) != 0u;
    const int satisfied = wait_all ? (value == mask) : (value != 0u);
    if (matched != NULL) *matched = satisfied ? value : 0u;
    return satisfied;
}

static int register_waiter_locked(hrt_event_t *event,
                                  const int task_id,
                                  const hrt_event_bits_t mask,
                                  const unsigned options) {
    if (task_id < 0 || task_id >= HARDRT_APP_MAX_TASKS) return -1;
    if (event->wait_active[task_id] != 0u) return -1;
    if (event->wait_count >= HARDRT_APP_MAX_TASKS) return -1;

    event->wait_q[event->wait_count] = (uint8_t)task_id;
    event->wait_count++;
    event->wait_active[task_id] = 1u;
    event->wait_options[task_id] = (uint8_t)options;
    event->wait_mask[task_id] = mask;
    event->wait_matched[task_id] = 0u;
    return 0;
}

static int set_locked(hrt_event_t *event, const hrt_event_bits_t bits) {
    const hrt_event_bits_t snapshot = event->bits | bits;
    hrt_event_bits_t clear_union = 0u;
    uint8_t write = 0u;
    int should_switch = 0;

    event->bits = snapshot;

    const uint8_t count = event->wait_count;
    for (uint8_t read = 0u; read < count; ++read) {
        const uint8_t task_id = event->wait_q[read];
        if (task_id >= HARDRT_APP_MAX_TASKS ||
            event->wait_active[task_id] == 0u) {
            continue;
        }

        hrt_event_bits_t matched = 0u;
        if (!event_matches(snapshot,
                           event->wait_mask[task_id],
                           event->wait_options[task_id],
                           &matched)) {
            event->wait_q[write++] = task_id;
            continue;
        }

        event->wait_matched[task_id] = matched;
        event->wait_active[task_id] = 0u;
        if ((event->wait_options[task_id] &
             (uint8_t)HRT_EVENT_CLEAR_ON_EXIT) != 0u) {
            clear_union |= matched;
        }

        hrt__make_ready((int)task_id);
        if (hrt__should_preempt_after_wake((int)task_id) != 0) {
            should_switch = 1;
        }
    }

    event->wait_count = write;
    event->bits = snapshot & ~clear_union;
    return should_switch;
}

void hrt_event_init(hrt_event_t *event) {
    if (event == NULL) return;
    memset(event, 0, sizeof(*event));
}

hrt_event_bits_t hrt_event_get(const hrt_event_t *event) {
    if (event == NULL) return 0u;

    hrt_port_crit_enter();
    const hrt_event_bits_t bits = event->bits;
    hrt_port_crit_exit();
    return bits;
}

static int event_set_common(hrt_event_t *event,
                            const hrt_event_bits_t bits,
                            const int is_isr,
                            int *need_switch) {
    if (event == NULL) {
        if (need_switch != NULL) *need_switch = 0;
        return -1;
    }

    hrt_port_crit_enter();
    const int should_switch = set_locked(event, bits);
    hrt_port_crit_exit();

    if (is_isr != 0) {
        if (need_switch != NULL) *need_switch = should_switch;
        if (should_switch != 0) hrt__pend_context_switch();
    } else if (should_switch != 0) {
        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
    }

    return 0;
}

int hrt_event_set(hrt_event_t *event, const hrt_event_bits_t bits) {
    return event_set_common(event, bits, 0, NULL);
}

int hrt_event_set_from_isr(hrt_event_t *event,
                           const hrt_event_bits_t bits,
                           int *need_switch) {
    return event_set_common(event, bits, 1, need_switch);
}

int hrt_event_clear(hrt_event_t *event, const hrt_event_bits_t bits) {
    if (event == NULL) return -1;

    hrt_port_crit_enter();
    event->bits &= ~bits;
    hrt_port_crit_exit();
    return 0;
}

int hrt_event_clear_from_isr(hrt_event_t *event, const hrt_event_bits_t bits) {
    return hrt_event_clear(event, bits);
}

int hrt_event_wait(hrt_event_t *event,
                   const hrt_event_bits_t mask,
                   const unsigned options,
                   hrt_event_bits_t *matched) {
    if (matched != NULL) *matched = 0u;
    if (event == NULL || mask == 0u || !event_options_valid(options)) return -1;

    const int me = hrt__get_current();
    if (me < 0 || me >= HARDRT_APP_MAX_TASKS) return -1;

    hrt_port_crit_enter();

    hrt_event_bits_t immediate = 0u;
    if (event_matches(event->bits, mask, options, &immediate)) {
        if ((options & (unsigned)HRT_EVENT_CLEAR_ON_EXIT) != 0u) {
            event->bits &= ~immediate;
        }
        hrt_port_crit_exit();
        if (matched != NULL) *matched = immediate;
        return 0;
    }

    _hrt_tcb_t *task = hrt__tcb(me);
    if (task == NULL || task->state != HRT_RUNNING ||
        register_waiter_locked(event, me, mask, options) != 0) {
        hrt_port_crit_exit();
        return -1;
    }

    task->state = HRT_BLOCKED;
    hrt__pend_context_switch();
    hrt_port_crit_exit();
    hrt_port_yield_to_scheduler();

    hrt_port_crit_enter();
    const hrt_event_bits_t result = event->wait_matched[me];
    event->wait_matched[me] = 0u;
    event->wait_mask[me] = 0u;
    event->wait_options[me] = 0u;
    hrt_port_crit_exit();

    if (result == 0u) return -1;
    if (matched != NULL) *matched = result;
    return 0;
}
