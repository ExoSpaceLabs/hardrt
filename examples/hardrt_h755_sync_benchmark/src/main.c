/* SPDX-License-Identifier: Apache-2.0 */
/* Diagnostic-only DWT baseline; independent of the production library build. */
#include <stdint.h>
#include <stddef.h>
#include "hardrt.h"
#include "stm32h7xx.h"

#define DEMCR (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
#define CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define ROWS 14u
#define WARMUP 32u
#define MAX_ITEM 1024u
#define STACK_WORDS 1024u
#ifndef HRT_SYNC_BENCH_SAMPLES
#define HRT_SYNC_BENCH_SAMPLES 1000u
#endif
extern volatile uint32_t hrt__bench_cs_last_cycles;
extern volatile uint32_t hrt__bench_cs_outer_count;
extern uint32_t SystemCoreClock;
typedef struct {
    uint32_t id, bytes, count;
    uint32_t api_min, api_avg, api_max;
    uint32_t masked_min, masked_avg, masked_max;
} timing_row_t;
typedef struct {
    uint32_t passed, error, core_hz, samples, row_count, profiled;
    timing_row_t rows[ROWS];
} timing_result_t;
_Static_assert(sizeof(timing_row_t) == 36u, "GDB row ABI");
_Static_assert(offsetof(timing_result_t, rows) == 24u, "GDB header ABI");
typedef struct {
    uint32_t count, api_min, api_max, cs_min, cs_max;
    uint64_t api_sum, cs_sum;
} stats_t;
static stats_t metrics[ROWS];
static volatile timing_result_t result;
static uint32_t stack[STACK_WORDS] __attribute__((aligned(8)));
static hrt_sem_t sem;
static hrt_mutex_t mutex;
static hrt_queue_t queue;
static uint8_t source[MAX_ITEM] __attribute__((aligned(8)));
static uint8_t target[MAX_ITEM] __attribute__((aligned(8)));
static uint8_t storage[MAX_ITEM] __attribute__((aligned(8)));
static const uint32_t sizes[5] = {4u, 16u, 64u, 256u, 1024u};

__attribute__((noinline, used))
void sync_benchmark_emit(const volatile timing_result_t *p) {
    __asm volatile("" : : "r"(p) : "memory");
    for (;;) __asm volatile("wfi");
}

static void complete(uint32_t error) {
    result.passed = error == 0u ? 1u : 0u;
    result.error = error;
    result.core_hz = SystemCoreClock;
    result.samples = HRT_SYNC_BENCH_SAMPLES;
    result.row_count = ROWS;
    result.profiled = 1u;
    for (uint32_t i = 0; i < ROWS; ++i) {
        stats_t *s = &metrics[i];
        volatile timing_row_t *r = &result.rows[i];
        r->id = i;
        r->bytes = i < 4u ? 0u : sizes[(i - 4u) / 2u];
        r->count = s->count;
        r->api_min = s->count ? s->api_min : 0u;
        r->api_avg = s->count ? (uint32_t)(s->api_sum / s->count) : 0u;
        r->api_max = s->api_max;
        r->masked_min = s->count ? s->cs_min : 0u;
        r->masked_avg = s->count ? (uint32_t)(s->cs_sum / s->count) : 0u;
        r->masked_max = s->cs_max;
    }
    sync_benchmark_emit(&result);
}
static void sample(uint32_t id, uint32_t iteration, uint32_t prior,
                   uint32_t begin, uint32_t end, int rc) {
    if (rc != 0) complete(10u + id);
    if (hrt__bench_cs_outer_count != prior + 1u) complete(50u + id);
    const uint32_t call_cycles = end - begin;
    const uint32_t masked = hrt__bench_cs_last_cycles;
    if (call_cycles < masked) complete(90u + id);
    if (iteration < WARMUP) return;
    stats_t *const s = &metrics[id];
    if (call_cycles < s->api_min) s->api_min = call_cycles;
    if (call_cycles > s->api_max) s->api_max = call_cycles;
    if (masked < s->cs_min) s->cs_min = masked;
    if (masked > s->cs_max) s->cs_max = masked;
    s->api_sum += call_cycles;
    s->cs_sum += masked;
    ++s->count;
}

static void worker(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < ROWS; ++i) {
        metrics[i].api_min = UINT32_MAX;
        metrics[i].cs_min = UINT32_MAX;
    }
    hrt_sem_init(&sem, 1u);
    hrt_mutex_init(&mutex);
    const uint32_t total = HRT_SYNC_BENCH_SAMPLES + WARMUP;
    for (uint32_t i = 0; i < total; ++i) {
        uint32_t before = hrt__bench_cs_outer_count;
        uint32_t start = CYCCNT;
        int rc = hrt_sem_try_take(&sem);
        uint32_t end = CYCCNT;
        sample(0u,i,before,start,end,rc);
        before = hrt__bench_cs_outer_count;
        start = CYCCNT;
        rc = hrt_sem_give(&sem);
        end = CYCCNT;
        sample(1u,i,before,start,end,rc);
        before = hrt__bench_cs_outer_count;
        start = CYCCNT;
        rc = hrt_mutex_try_lock(&mutex);
        end = CYCCNT;
        sample(2u,i,before,start,end,rc);
        before = hrt__bench_cs_outer_count;
        start = CYCCNT;
        rc = hrt_mutex_unlock(&mutex);
        end = CYCCNT;
        sample(3u,i,before,start,end,rc);
    }
    for (uint32_t k = 0; k < 5u; ++k) {
        const uint32_t n = sizes[k];
        hrt_queue_init(&queue, storage, 1u, n);
        for (uint32_t j = 0; j < n; ++j) {
            source[j] = (uint8_t)((j*37u + k + 1u) & 0xffu);
            target[j] = 0u;
        }
        for (uint32_t i = 0; i < total; ++i) {
            uint32_t before = hrt__bench_cs_outer_count;
            uint32_t start = CYCCNT;
            int rc = hrt_queue_try_send(&queue, source);
            uint32_t end = CYCCNT;
            sample(4u + k*2u,i,before,start,end,rc);
            before = hrt__bench_cs_outer_count;
            start = CYCCNT;
            rc = hrt_queue_try_recv(&queue, target);
            end = CYCCNT;
            sample(5u + k*2u,i,before,start,end,rc);
        }
        for (uint32_t j = 0; j < n; ++j)
            if (source[j] != target[j]) complete(130u + k);
    }
    for (uint32_t i = 0; i < ROWS; ++i)
        if (metrics[i].count != HRT_SYNC_BENCH_SAMPLES) complete(200u+i);
    complete(0u);
}

static void hold_cm4(void) {
#define RCC_BASE_NEW 0x58024400UL
#define RCC_GCR (*(volatile uint32_t *)(RCC_BASE_NEW + 0u))
#define RCC_GRSTCSETR (*(volatile uint32_t *)(RCC_BASE_NEW + 8u))
    RCC_GCR &= ~1u;
    RCC_GRSTCSETR = 1u;
}
int main(void) {
    SystemInit();
    hold_cm4();
    DEMCR |= (1u << 24);
    CYCCNT = 0u;
    DWT_CTRL |= 1u;
    if ((DWT_CTRL & 1u) == 0u) complete(1u);
    const hrt_config_t cfg = {
       .tick_hz = 1000u, .policy = HRT_SCHED_PRIORITY,
       .default_slice = 0u, .core_hz = SystemCoreClock,
       .tick_src = HRT_TICK_EXTERNAL
    };
    if (hrt_init(&cfg) != HRT_OK) complete(2u);
    const hrt_task_attr_t attr = {.priority = HRT_PRIO0, .timeslice = 0u};
    if (hrt_create_task(worker,NULL,stack,STACK_WORDS,&attr) < 0) complete(3u);
    hrt_start();
    complete(4u);
    return 1;
}
