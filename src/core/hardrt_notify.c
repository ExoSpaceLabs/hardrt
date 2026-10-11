/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_notify.h"
#include "hardrt_port_int.h"
#include "hardrt_deadline.h"

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

static int timeout_cancel(void *context, const int task_id) {
    (void)context;
    _hrt_tcb_t *const task = notify_target_locked(task_id);
    if (task == NULL || task->notify_waiting == HRT_NOTIFY_WAIT_NONE) {
        return -1;
    }
    task->notify_waiting = HRT_NOTIFY_WAIT_NONE;
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
        hrt__wait_satisfied_locked(task_id);
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

hrt_wait_result_t hrt_task_notify_wait_until(const uint32_t clear_on_entry,
                                             const uint32_t clear_on_exit,
                                             const hrt_tick_t deadline,
                                             uint32_t *value) {
    if (value != NULL) *value = 0u;

    const int me = hrt__current_running_app_task();
    if (me < 0) return HRT_WAIT_INVALID_CONTEXT;

    hrt_port_crit_enter();
    _hrt_tcb_t *task = notify_target_locked(me);
    if (task == NULL || task->state != HRT_RUNNING ||
        task->notify_waiting != HRT_NOTIFY_WAIT_NONE) {
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }

    hrt_tick_t distance = 0u;
    const hrt_deadline_relation_t relation =
        hrt__deadline_relation(hrt_tick_now(), deadline, &distance);
    if (relation == HRT_DEADLINE_AMBIGUOUS) {
        hrt_port_crit_exit();
        return HRT_WAIT_INVALID_DEADLINE;
    }

    task->notify_value &= ~clear_on_entry;
    if (task->notify_pending != 0u) {
        const uint32_t observed = task->notify_value;
        task->notify_value &= ~clear_on_exit;
        task->notify_pending = 0u;
        hrt_port_crit_exit();
        if (value != NULL) *value = observed;
        return HRT_WAIT_OK;
    }

    if (relation != HRT_DEADLINE_FUTURE) {
        hrt_port_crit_exit();
        return HRT_WAIT_TIMEOUT;
    }

    task->notify_waiting = HRT_NOTIFY_WAIT_VALUE;
    if (hrt__wait_timeout_arm_locked(me, distance, timeout_cancel, NULL) != 0) {
        task->notify_waiting = HRT_NOTIFY_WAIT_NONE;
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }

    task->state = HRT_BLOCKED;
    hrt__pend_context_switch();
    hrt_port_crit_exit();
    hrt_port_yield_to_scheduler();

    hrt_port_crit_enter();
    task = notify_target_locked(me);
    const hrt_wait_result_t result = hrt__wait_result_take_locked(me);
    if (task == NULL) {
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }

    if (result != HRT_WAIT_OK) {
        hrt_port_crit_exit();
        return result;
    }
    if (task->notify_pending == 0u ||
        task->notify_waiting != HRT_NOTIFY_WAIT_NONE) {
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }

    const uint32_t observed = task->notify_value;
    task->notify_value &= ~clear_on_exit;
    task->notify_pending = 0u;
    hrt_port_crit_exit();

    if (value != NULL) *value = observed;
    return HRT_WAIT_OK;
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

hrt_wait_result_t hrt_task_notify_take_until(const int clear_count_on_exit,
                                             const hrt_tick_t deadline,
                                             uint32_t *value) {
    if (value != NULL) *value = 0u;

    const int me = hrt__current_running_app_task();
    if (me < 0) return HRT_WAIT_INVALID_CONTEXT;

    for (;;) {
        hrt_port_crit_enter();
        _hrt_tcb_t *task = notify_target_locked(me);
        if (task == NULL || task->state != HRT_RUNNING ||
            task->notify_waiting != HRT_NOTIFY_WAIT_NONE) {
            hrt_port_crit_exit();
            return HRT_WAIT_ERROR;
        }

        hrt_tick_t distance = 0u;
        const hrt_deadline_relation_t relation =
            hrt__deadline_relation(hrt_tick_now(), deadline, &distance);
        if (relation == HRT_DEADLINE_AMBIGUOUS) {
            hrt_port_crit_exit();
            return HRT_WAIT_INVALID_DEADLINE;
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
            if (value != NULL) *value = observed;
            return HRT_WAIT_OK;
        }

        if (relation != HRT_DEADLINE_FUTURE) {
            hrt_port_crit_exit();
            return HRT_WAIT_TIMEOUT;
        }

        task->notify_waiting = HRT_NOTIFY_WAIT_TAKE;
        if (hrt__wait_timeout_arm_locked(me, distance, timeout_cancel, NULL) != 0) {
            task->notify_waiting = HRT_NOTIFY_WAIT_NONE;
            hrt_port_crit_exit();
            return HRT_WAIT_ERROR;
        }

        task->state = HRT_BLOCKED;
        hrt__pend_context_switch();
        hrt_port_crit_exit();
        hrt_port_yield_to_scheduler();

        hrt_port_crit_enter();
        const hrt_wait_result_t result = hrt__wait_result_take_locked(me);
        hrt_port_crit_exit();
        if (result != HRT_WAIT_OK) return result;

        /* A successful notification wake guarantees a non-zero value for TAKE,
         * but loop through the same absolute deadline to keep the state check
         * centralized and robust against future notification semantics. */
    }
}
