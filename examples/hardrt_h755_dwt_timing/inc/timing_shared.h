#ifndef HARDRT_DWT_TIMING_SHARED_H
#define HARDRT_DWT_TIMING_SHARED_H

#include <stdint.h>

#define HRT_TIMING_CASE_EVENT_TO_TASK 1u
#define HRT_TIMING_CASE_SEM_ISR_READY 2u

#ifndef HRT_TIMING_CASE_ID
#define HRT_TIMING_CASE_ID HRT_TIMING_CASE_EVENT_TO_TASK
#endif

#ifndef HRT_TIMING_EVENT_HZ
#define HRT_TIMING_EVENT_HZ 1000u
#endif

#ifndef HRT_TIMING_TARGET_SAMPLES
#define HRT_TIMING_TARGET_SAMPLES 10000u
#endif

typedef struct {
    volatile uint32_t min;
    volatile uint32_t max;
    volatile uint32_t avg;
    volatile uint32_t count;
    volatile uint64_t sum;
} hrt_timing_stats_t;

extern volatile hrt_timing_stats_t g_timing_stats;
extern volatile uint32_t g_timing_start_cycles;
extern volatile uint32_t g_timing_case_id;
extern volatile uint32_t g_timing_event_hz;
extern volatile uint32_t g_timing_target_samples;
extern volatile uint32_t g_example_error;

static inline __attribute__((always_inline))
void hrt_timing_stats_record(volatile hrt_timing_stats_t *s, uint32_t value) {
    if (s->count >= g_timing_target_samples) return;
    if (value < s->min) s->min = value;
    if (value > s->max) s->max = value;
    s->sum += value;
    s->count++;
    s->avg = (uint32_t)(s->sum / s->count);
}

#endif /* HARDRT_DWT_TIMING_SHARED_H */
