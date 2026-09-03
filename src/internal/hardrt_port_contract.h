#ifndef HARDRT_PORT_CONTRACT_INTERNAL_H
#define HARDRT_PORT_CONTRACT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Private HardRT core <-> port contract.
 *
 * These functions are implementation hooks, not application APIs. A port must
 * implement the hooks below with bounded, documented behavior appropriate to
 * its execution model. Cortex-M timing qualification applies separately from
 * the POSIX functional-simulation port.
 */

void hrt_port_start_systick(uint32_t tick_hz);
void hrt_port_idle_wait(void);
void hrt_port_enter_scheduler(void);
void hrt_port_yield_to_scheduler(void);
void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t *stack_base, size_t words);
void hrt_port_crit_enter(void);
void hrt_port_crit_exit(void);
void hrt_port_sp_valid(uintptr_t sp);

/* Port-owned scheduling/context entry points retained with their existing names
 * during the v0.5 contract cleanup. */
void hrt__pend_context_switch(void);
void hrt__task_trampoline(void);
void hrt__init_idle_task(void);

#ifdef __cplusplus
}
#endif

#endif /* HARDRT_PORT_CONTRACT_INTERNAL_H */
