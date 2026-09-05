/* SPDX-License-Identifier: Apache-2.0 */
/* Compile-only probe extracted from the public C API documentation. */
#include "hardrt.h"
#include "hardrt_event.h"
#include "hardrt_mutex.h"
#include "hardrt_notify.h"
#include "hardrt_queue.h"
#include "hardrt_sem.h"

#include <stdint.h>

int hardrt_doc_c_smoke(void) {
    const hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_EXTERNAL
    };
    const hrt_task_attr_t attr = {
        .priority = HRT_PRIO0,
        .timeslice = 0u
    };

    hrt_sem_t sem;
    hrt_mutex_t mutex;
    hrt_queue_t queue;
    uint32_t queue_storage[4] = {0u};
    hrt_event_t event;
    hrt_event_bits_t matched = 0u;
    uint32_t notify_value = 0u;
    int need_switch = 0;

    hrt_sem_init(&sem, 0u);
    hrt_sem_init_counting(&sem, 0u, 4u);
    (void)hrt_sem_try_take(&sem);
    (void)hrt_sem_give(&sem);
    (void)hrt_sem_give_from_isr(&sem, &need_switch);

    hrt_mutex_init(&mutex);
    (void)hrt_mutex_try_lock(&mutex);
    (void)hrt_mutex_unlock(&mutex);

    hrt_queue_init(&queue, queue_storage, 4u, sizeof(queue_storage[0]));
    (void)hrt_queue_try_send(&queue, &queue_storage[0]);
    (void)hrt_queue_try_recv(&queue, &queue_storage[0]);
    (void)hrt_queue_try_send_from_isr(&queue, &queue_storage[0], &need_switch);
    (void)hrt_queue_try_recv_from_isr(&queue, &queue_storage[0], &need_switch);
    (void)hrt_queue_count(&queue);

    hrt_event_init(&event);
    (void)hrt_event_get(&event);
    (void)hrt_event_set(&event, 0x1u);
    (void)hrt_event_clear(&event, 0x1u);
    (void)hrt_event_set_from_isr(&event, 0x2u, &need_switch);
    (void)hrt_event_clear_from_isr(&event, 0x2u);
    (void)hrt_event_wait(&event, 0x3u,
                         HRT_EVENT_WAIT_ALL | HRT_EVENT_CLEAR_ON_EXIT,
                         &matched);

    (void)hrt_task_notify(0, 0x1u, HRT_NOTIFY_SET_BITS);
    (void)hrt_task_notify_from_isr(0, 0x2u, HRT_NOTIFY_OVERWRITE,
                                   &need_switch);
    (void)hrt_task_notify_wait(0u, UINT32_MAX, &notify_value);
    (void)hrt_task_notify_take(1);

    (void)hrt_init(&cfg);
    (void)attr;
    (void)hrt_version_string();
    (void)hrt_version_u32();
    (void)hrt_port_name();
    (void)hrt_port_id();
    (void)hrt_tick_now();
    (void)hrt_now_ms();
    hrt_set_policy(HRT_SCHED_PRIORITY);
    hrt_set_default_timeslice(0u);
    hrt_tick_from_isr();

    return (int)(matched ^ notify_value ^ (uint32_t)need_switch);
}
