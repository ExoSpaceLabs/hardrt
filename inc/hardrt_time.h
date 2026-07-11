#ifndef HARDRT_TIME_H
#define HARDRT_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Advance the kernel tick from an application-owned timer ISR.
 *
 * @details This public entry point is used only when the kernel was initialized
 * with HRT_TICK_EXTERNAL. It advances time accounting, wakes expired sleeping
 * tasks, updates round-robin slice accounting, and requests rescheduling when
 * required.
 *
 * When the selected tick source is HRT_TICK_SYSTICK, the current implementation
 * ignores this call. Port-owned tick handlers call the private hrt__tick_isr()
 * function instead.
 *
 * This function never performs a task-context switch directly. A caller does
 * not need to invoke a second yield-from-ISR function.
 */
void hrt_tick_from_isr(void);

#ifdef __cplusplus
}
#endif

#endif