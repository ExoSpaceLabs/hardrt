#ifndef HARDRT_H755_READY_SELECTION_HOOKS_H
#define HARDRT_H755_READY_SELECTION_HOOKS_H

#include <stdint.h>

/* Enabled only by HARDRT_TIMING_PROFILE=scheduler for the dedicated
 * diagnostic READY-selection image. Production profile=none is hook-free. */
extern volatile uint32_t g_ready_pick_start_cycles;
extern volatile uint32_t g_ready_pick_cycles;

#define HRT_TIMING_READY_PICK_BEGIN() do {                         \
    g_ready_pick_start_cycles = *(volatile uint32_t *)0xE0001004u; \
} while (0)

#define HRT_TIMING_READY_PICK_END() do {                                    \
    g_ready_pick_cycles = *(volatile uint32_t *)0xE0001004u                 \
                          - g_ready_pick_start_cycles;                      \
} while (0)

#endif /* HARDRT_H755_READY_SELECTION_HOOKS_H */
