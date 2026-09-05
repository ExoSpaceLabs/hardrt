#include "hardrt.h"
#include <stdio.h>

static uint32_t receiver_stack[2048], producer_stack[2048];
static hrt_event_t signals;
static int receiver_id = -1;

static void receiver(void *arg) {
    (void)arg;
    for (;;) {
        hrt_event_bits_t matched = 0u;
        if (hrt_event_wait(&signals, 0x1u, HRT_EVENT_CLEAR_ON_EXIT, &matched) == 0) {
            printf("[event] matched=0x%08x\n", (unsigned)matched);
        }

        uint32_t value = 0u;
        if (hrt_task_notify_wait(0u, UINT32_MAX, &value) == 0) {
            printf("[notify] value=%u\n", (unsigned)value);
        }
    }
}

static void producer(void *arg) {
    (void)arg;
    uint32_t sequence = 1u;
    for (;;) {
        hrt_sleep(200u);
        hrt_event_set(&signals, 0x1u);

        hrt_sleep(100u);
        hrt_task_notify(receiver_id, sequence++, HRT_NOTIFY_OVERWRITE);
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
    if (hrt_create_task(producer, NULL, producer_stack,
                        sizeof(producer_stack) / sizeof(producer_stack[0]), &low) < 0) {
        puts("create producer failed");
        return 1;
    }

    hrt_start();
    return 0;
}
