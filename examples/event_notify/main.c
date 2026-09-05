#include "hardrt.h"
#include <stdio.h>

static uint32_t receiver_stack[2048], producer_a_stack[2048], producer_b_stack[2048];
static hrt_event_t signals;
static int receiver_id = -1;

static void receiver(void *arg) {
    (void)arg;
    for (;;) {
        hrt_event_bits_t matched = 0u;
        if (hrt_event_wait(&signals,
                           0x3u,
                           HRT_EVENT_WAIT_ALL | HRT_EVENT_CLEAR_ON_EXIT,
                           &matched) == 0) {
            printf("[event] matched=0x%08x\n", (unsigned)matched);
        }

        /* Producer A publishes this notification before producer B completes
         * the event mask. The notification therefore remains pending while this
         * task is blocked on the event and is consumed immediately here. */
        uint32_t value = 0u;
        if (hrt_task_notify_wait(0u, UINT32_MAX, &value) == 0) {
            printf("[notify] value=%u\n", (unsigned)value);
        }
    }
}

static void producer_a(void *arg) {
    (void)arg;
    uint32_t sequence = 1u;
    for (;;) {
        hrt_sleep(200u);
        hrt_task_notify(receiver_id, sequence++, HRT_NOTIFY_OVERWRITE);
        hrt_event_set(&signals, 0x1u);
        hrt_sleep(400u);
    }
}

static void producer_b(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sleep(300u);
        hrt_event_set(&signals, 0x2u);
        hrt_sleep(300u);
    }
}

int main(void) {
    const hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_SYSTICK
    };
    if (hrt_init(&cfg) != HRT_OK) {
        puts("HardRT init failed");
        return 1;
    }

    hrt_event_init(&signals);
    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 3u};
    const hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 3u};

    receiver_id = hrt_create_task(receiver, NULL, receiver_stack,
                                  sizeof(receiver_stack) / sizeof(receiver_stack[0]), &high);
    if (receiver_id < 0) {
        puts("create receiver failed");
        return 1;
    }
    if (hrt_create_task(producer_a, NULL, producer_a_stack,
                        sizeof(producer_a_stack) / sizeof(producer_a_stack[0]), &low) < 0) {
        puts("create producer A failed");
        return 1;
    }
    if (hrt_create_task(producer_b, NULL, producer_b_stack,
                        sizeof(producer_b_stack) / sizeof(producer_b_stack[0]), &low) < 0) {
        puts("create producer B failed");
        return 1;
    }

    hrt_start();
    return 0;
}
