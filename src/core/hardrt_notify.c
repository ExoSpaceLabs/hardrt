/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_notify.h"
#include "hardrt_port_int.h"

#include <limits.h>

#define HRT_NOTIFY_WAIT_NONE 0u
#define HRT_NOTIFY_WAIT_VALUE 1u
#define HRT_NOTIFY_WAIT_TAKE 2u

static int notify_action_valid(const hrt_notify_action_t action) {
    return action == HRT_NOTIFY_SET_BITS ||
           action == HRT_NOTIFY_OVERWRITE ||
           action == HRT_NOTIFY_NO_OVERWRITE ||
           action == HRT_NOTIFY_INCREMENT;
}

static _hrt_tcb_t *notify_target_locked(const int task_id) {
    if (task_id < 0 || task_id >= HARDRT_APP_MAX_TASKS) return NULL;

    _hrt_tcb_t *task = hrt__tcb(task_id);
    if (task == NULL || task->slot_state != HRT_SLOT_USED ||
        task->state == HRT_EXITED) {
        return NULL;
    }
    return task;
}

static int apply_notification_locked(_hrt_tcb_t *task,
                                     const uint32_t value,
                                     const hrt_notify_action_t action) {
    switch (action) {
        case HRT_NOTIFY_SET_BITS:
            task->notify_value |= value;
            break;
        case HRT_NOTIFY_OVERWRITE:
            task->notify_value = value;
            break;
        case HRT_NOTIFY_NO_OVERWRITE:
            if (task->notify_pending != 0u) return -1;
            task->notify_value = value;
            break;
        case HRT_NOTIFY_INCREMENT:
            if (task->notify_value != UINT32_MAX) task->notify_value++;
            break;
        default:
            return -1;
    }

    task->notify_pending = 1u;
    return 0;
}

static int notification_satisfies_waiter(const _hrt_tcb_t *task) {
    if (task->notify_waiting == HRT_NOTIFY_WAIT_VALUE) return 1;
    if (task->notify_waiting == HRT_NOTIFY_WAIT_TAKE) {
        return task->notify_value != 0u;
    }
    return 0;
}

static int notify_common(const int task_id,
                         const uint32_t value,
                         const hrt_notify_action_t action,
                         const int is_isr,
                         int *need_switch) {
    int should_switch = 0;

    if (need_switch != NULL) *need_switch = 0;
    if (!notify_action_valid(action)) return -1;

    hrt_port_crit_enter();
    _hrt_tcb_t *task = notify_target_locked(task_id);
    if (task == NULL || apply_notification_locked(task, value, action) != 0) {
        hrt_port_crit_exit();
        return -1;
    }

    if (task->state == HRT_BLOCKED && notification_satisfies_waiter(task)) {
        task->notify_waiting = HRT_NOTIFY_WAIT_NONE;
        hrt__make_ready(task_id);
        should_switch = hrt__should_preempt_after_wake(task_id);
    }
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

int hrt_task_notify(const int task_id,
                    const uint32_t value,
                    const hrt_notify_action_t action) {
    return notify_common(task_id, value, action, 0, NULL);
}

int hrt_task_notify_from_isr(const int task_id,
                             const uint32_t value,
                             const hrt_notify_action_t action,
                             int *need_switch) {
    return notify_common(task_id, value, action, 1, need_switch);
}

int hrt_task_notify_wait(const uint32_t clear_on_entry,
                         const uint32_t clear_on_exit,
                         uint32_t *value) {
    if (value != NULL) *value = 0u;

    const int me = hrt__get_current();
    if (me < 0 || me >= HARDRT_APP_MAX_TASKS) return -1;

    hrt_port_crit_enter();
    _hrt_tcb_t *task = notify_target_locked(me);
    if (task == NULL || task->state != HRT_RUNNING ||
        task->notify_waiting != HRT_NOTIFY_WAIT_NONE) {
        hrt_port_crit_exit();
        return -1;
    }

    task->notify_value &= ~clear_on_entry;
    if (task->notify_pending == 0u) {
        task->notify_waiting = HRT_NOTIFY_WAIT_VALUE;
        task->state = HRT_BLOCKED;
        hrt__pend_context_switch();
        hrt_port_crit_exit();
        hrt_port_yield_to_scheduler();
        hrt_port_crit_enter();

        task = notify_target_locked(me);
        if (task == NULL || task->notify_pending == 0u ||
            task->notify_waiting != HRT_NOTIFY_WAIT_NONE) {
            hrt_port_crit_exit();
            return -1;
        }
    }

    const uint32_t observed = task->notify_value;
    task->notify_value &= ~clear_on_exit;
    task->notify_pending = 0u;
    hrt_port_crit_exit();

    if (value != NULL) *value = observed;
    return 0;
}

uint32_t hrt_task_notify_take(const int clear_count_on_exit) {
    const int me = hrt__get_current();
    if (me < 0 || me >= HARDRT_APP_MAX_TASKS) return 0u;

    for (;;) {
        hrt_port_crit_enter();
        _hrt_tcb_t *task = notify_target_locked(me);
        if (task == NULL || task->state != HRT_RUNNING ||
            task->notify_waiting != HRT_NOTIFY_WAIT_NONE) {
            hrt_port_crit_exit();
            return 0u;
        }

        if (task->notify_value != 0u) {
            const uint32_t observed = task->notify_value;
            if (clear_count_on_exit != 0) {
                task->notify_value = 0u;
            } else {
                task->notify_value--;
            }
            task->notify_pending = task->notify_value != 0u ? 1u : 0u;
            hrt_port_crit_exit();
            return observed;
        }

        /* A zero-valued pending update does not satisfy counting-take. The
         * pending marker remains valid for overwrite/no-overwrite semantics,
         * while an increment can still make the count non-zero and wake us. */
        task->notify_waiting = HRT_NOTIFY_WAIT_TAKE;
        task->state = HRT_BLOCKED;
        hrt__pend_context_switch();
        hrt_port_crit_exit();
        hrt_port_yield_to_scheduler();
    }
}
