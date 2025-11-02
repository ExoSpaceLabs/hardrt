#ifndef HEARTOS_TIME_H
#define HEARTOS_TIME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {

#endif

/**
 * @brief Advance the kernel tick from a timer ISR.
 *
 * @details Called once per hardware tick to update time accounting and wake
 * sleeping tasks whose deadlines have expired.
 * - If the port owns the tick (e.g., SysTick), the port's ISR should call this.
 * - If the application owns a hardware timer (HRT_TICK_EXTERNAL), that ISR should call this.
 *
 * This function does not perform a context switch directly; the port or ISR
 * should request one in a port-appropriate way after calling this function.
 */
void hrt_tick_from_isr(void);


// internal tick isr function.
/**
 * @brief Kernel tick handler invoked by the port at each system tick.
 * @note Ports must call this from their timer ISR or tick thread context.
 *       The function is safe to call at interrupt context; it accounts time,
 *       wakes sleeping tasks whose deadlines expired, and requests reschedule.
 */
//void hrt__tick_isr(void);

#ifdef __cplusplus
}
#endif
#endif
