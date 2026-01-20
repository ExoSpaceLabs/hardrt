#include "hardrt.h"
#include "hardrt_queue.h"

#include <stdio.h>
#include <stdint.h>

/* POSIX port demo: producer -> queue -> consumer */

static uint32_t stack_prod[2048];
static uint32_t stack_cons[2048];

static hrt_queue_t q_u32;
static uint8_t q_storage[32 * sizeof(uint32_t)];

static void producer(void* arg) {
    (void)arg;
    uint32_t v = 0;

    for (;;) {
        /* Blocks (waits) if the queue is full. */
        (void)hrt_queue_send(&q_u32, &v);
        printf("[producer] sent %u\n", (unsigned)v);
        fflush(stdout);
        v++;
        hrt_sleep(200);
    }
}

static void consumer(void* arg) {
    (void)arg;
    uint32_t v;

    for (;;) {
        /* Blocks (waits) if the queue is empty. */
        (void)hrt_queue_recv(&q_u32, &v);
        printf("[consumer] got  %u\n", (unsigned)v);
        fflush(stdout);
        hrt_sleep(500);
    }
}

int main(void) {
    printf("HardRT version: %s (0x%06X), port: %s (id=%d)\n",
           hrt_version_string(), hrt_version_u32(),
           hrt_port_name(), hrt_port_id());

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy  = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5
    };

    if (hrt_init(&cfg) != 0) {
        puts("HardRT init failed");
        return 1;
    }

    /* init queue before starting tasks */
    hrt_queue_init(&q_u32, q_storage, 32, sizeof(uint32_t));

    const hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 0 };
    const hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 5 };

    if (hrt_create_task(producer, 0, stack_prod, sizeof(stack_prod)/sizeof(stack_prod[0]), &p0) < 0)
        puts("create producer failed");

    if (hrt_create_task(consumer, 0, stack_cons, sizeof(stack_cons)/sizeof(stack_cons[0]), &p1) < 0)
        puts("create consumer failed");

    hrt_start();
    return 0;
}
