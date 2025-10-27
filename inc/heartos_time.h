#ifndef HEARTOS_TIME_H
#define HEARTOS_TIME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Kernel tick handler invoked by the port at each system tick.
     * @note Ports must call this from their timer ISR or tick thread context.
     *       The function is safe to call at interrupt context; it accounts time,
     *       wakes sleeping tasks whose deadlines expired, and requests reschedule.
     */
    void hrt__tick_isr(void);

#ifdef __cplusplus
}
#endif
#endif