#ifndef HEARTOS_TIME_H
#define HEARTOS_TIME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {

#endif

// Called from a timer ISR to advance the kernel tick.
// - If the port owns the tick (SysTick), the port's ISR calls this.
// - If the app owns a hardware timer (EXTERNAL), that ISR calls this.
// This function only updates time and wakes sleepers. It does NOT force a context
// switch; the port/ISR should request one in a port-appropriate way.
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
