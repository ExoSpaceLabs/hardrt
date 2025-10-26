#ifndef HEARTOS_TIME_H
#define HEARTOS_TIME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

    /* Internal: called from port tick ISR/thread */
    void hrt__tick_isr(void);

#ifdef __cplusplus
}
#endif
#endif