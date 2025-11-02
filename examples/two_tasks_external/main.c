#include "heartos.h"
#include "heartos_time.h"
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

/* Two simple tasks, like examples/two_tasks, but the tick is driven by a POSIX thread
 * that sleeps according to cfg.tick_hz and calls hrt_tick_from_isr() each period.
 */

static uint32_t stack_a[2048];
static uint32_t stack_b[2048];

static atomic_int g_run_tick_thread = 1;
static uint32_t g_tick_hz = 200; /* default; may be overridden in cfg below */

static void *tick_thread_fn(void *arg) {
    (void)arg;
    /* Sleep period derived from g_tick_hz */
    struct timespec ts;
    if (g_tick_hz == 0) g_tick_hz = 1000;
    long nsec = 1000000000L / (long)g_tick_hz;
    ts.tv_sec = 0;
    ts.tv_nsec = nsec;
    while (atomic_load(&g_run_tick_thread)) {
        /* Advance kernel time by one tick */
        hrt_tick_from_isr();
        /* Sleep roughly one tick period */
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void taskA(void* arg){
    (void)arg;
    static uint32_t counter = 0;
    for(;;){
        printf("[A] External tick; counter [%u]\n", counter);
        fflush(stdout);
        hrt_sleep(500); /* 500 ms */
        counter++;
    }
}

static void taskB(void* arg){
    (void)arg;
    for(;;){
        puts("[B] External tock -----");
        fflush(stdout);
        hrt_sleep(1000); /* 1 second */
    }
}

int main(void){
    printf("HeaRTOS version: %s (0x%06X), port: %s (id=%d)\n",
           hrt_version_string(), hrt_version_u32(),
           hrt_port_name(), hrt_port_id());

    /* Configure external tick mode */
    hrt_config_t cfg = {
        .tick_hz = g_tick_hz,
        .policy  = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .tick_src = HRT_TICK_EXTERNAL,
        .core_hz = 0, /* not used for external tick */
    };

    if (hrt_init(&cfg) != 0){
        puts("HeaRTOS init failed");
        return 1;
    }

    /* Start the POSIX tick thread to simulate a hardware timer */
    pthread_t tick_thread;
    if (pthread_create(&tick_thread, NULL, tick_thread_fn, NULL) != 0){
        puts("Failed to start tick thread");
        return 1;
    }

    /* Two demo tasks */
    hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 0 };
    hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 5 };

    if (hrt_create_task(taskA, 0, stack_a, sizeof(stack_a)/sizeof(stack_a[0]), &p0) < 0)
        puts("create taskA failed");
    if (hrt_create_task(taskB, 0, stack_b, sizeof(stack_b)/sizeof(stack_b[0]), &p1) < 0)
        puts("create taskB failed");

    /* Enter scheduler (never returns on POSIX). The tick thread will keep advancing time. */
    hrt_start();

    /* Not reached under POSIX; if a port returns, stop the tick thread */
    atomic_store(&g_run_tick_thread, 0);
    pthread_join(tick_thread, NULL);
    return 0;
}
