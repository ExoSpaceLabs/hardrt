/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HARDRT_NOTIFY_H
#define HARDRT_NOTIFY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    HRT_NOTIFY_SET_BITS = 0,
    HRT_NOTIFY_OVERWRITE,
    HRT_NOTIFY_NO_OVERWRITE,
    HRT_NOTIFY_INCREMENT
} hrt_notify_action_t;

/**
 * @brief Update one application task's private notification value.
 *
 * The target must be a live application task ID returned by hrt_create_task().
 * A successful update marks the notification pending. HRT_NOTIFY_INCREMENT
 * increments by one with UINT32_MAX saturation and ignores value.
 *
 * @return 0 on success, -1 for an invalid target/action or rejected
 * HRT_NOTIFY_NO_OVERWRITE operation.
 */
int hrt_task_notify(int task_id, uint32_t value, hrt_notify_action_t action);

/** ISR-safe equivalent of hrt_task_notify(). */
int hrt_task_notify_from_isr(int task_id,
                             uint32_t value,
                             hrt_notify_action_t action,
                             int *need_switch);

/**
 * @brief Wait indefinitely for a notification on the calling task.
 *
 * clear_on_entry is applied before checking pending state. On success, value
 * receives the pre-exit-clear notification value, clear_on_exit is applied,
 * and the pending state is consumed.
 *
 * @return 0 on success, -1 when called outside a live application task.
 */
int hrt_task_notify_wait(uint32_t clear_on_entry,
                         uint32_t clear_on_exit,
                         uint32_t *value);

/**
 * @brief Counting-notification convenience wait.
 *
 * Blocks until the calling task's notification value is non-zero. Returns the
 * pre-consumption value. If clear_count_on_exit is non-zero, the stored value
 * becomes zero; otherwise it is decremented by one.
 *
 * @return The value observed before consumption, or 0 for invalid task context.
 */
uint32_t hrt_task_notify_take(int clear_count_on_exit);

#ifdef __cplusplus
}
#endif

#endif /* HARDRT_NOTIFY_H */
