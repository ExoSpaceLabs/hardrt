#ifndef HARDRT_PORT_CONTRACT_INTERNAL_H
#define HARDRT_PORT_CONTRACT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "hardrt_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Authoritative private HardRT core <-> port contract.
 *
 * This header is never installed. A port implementation should depend on this
 * header instead of redeclaring kernel internals locally.
 *
 * Cortex-M qualification requires every hook used in a timing-sensitive path
 * to have bounded behavior under the supported configuration. POSIX implements
 * the same logical contract but remains a functional simulation environment.
 */

/* ---------------- Hooks that every port must implement ---------------- */

/* Called by hrt_init() during initialization.
 * Context: task/startup context, never ISR.
 * Blocking: must not block or dispatch a task.
 * Ordering: configure the selected tick/context-switch mechanism only; scheduler
 * entry happens later through hrt_port_enter_scheduler().
 */
void hrt_port_start_systick(uint32_t tick_hz);

/* Called when the port has no application task to execute.
 * Context: scheduler/idle context.
 * Blocking: may wait for an interrupt/event; must not modify kernel queues.
 */
void hrt_port_idle_wait(void);

/* Called by hrt_start() after an initial reschedule has been requested.
 * Context: startup/task context.
 * Blocking: may not return on embedded ports; null port may return.
 */
void hrt_port_enter_scheduler(void);

/* Called after a task changes state or voluntarily yields.
 * Context: task context only.
 * Blocking: transfers/pends control to the scheduler; never callable from ISR.
 */
void hrt_port_yield_to_scheduler(void);

/* Called by hrt_create_task() before the task becomes READY.
 * Context: task/startup context.
 * Blocking: must not block.
 * Ordering: construct/record the initial context and store required port state
 * before returning success to the core. Return 0 only when the context is fully
 * usable; return a negative value on failure so the core can roll the task slot
 * back to UNUSED without publishing it to READY storage.
 */
int hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                uint32_t *stack_base, size_t words);

/* Protect kernel scheduler/synchronization state.
 * Context: task context and supported kernel-aware ISR context.
 * Blocking: must not block; nesting is required.
 * Ordering: the outermost exit restores the pre-entry masking/protection state.
 */
void hrt_port_crit_enter(void);
void hrt_port_crit_exit(void);

/* Optional validation operation used by architecture/debug paths.
 * Context: kernel/port context, never application API.
 * Blocking: must not block.
 */
void hrt_port_sp_valid(uintptr_t sp);

/* Request a reschedule without directly switching task context.
 * Context: task or supported ISR context.
 * Blocking: non-blocking and safe to request repeatedly.
 * Cortex-M: pend PendSV. POSIX: set the pending scheduler flag.
 */
void hrt__pend_context_switch(void);

/* Enter the current task function and delete the task if it returns.
 * Context: first instruction path of a selected task.
 * Blocking: application task behavior is outside the hook itself; the wrapper
 * must preserve the task-return contract.
 */
void hrt__task_trampoline(void);

/* Initialize the port's idle representation.
 * Context: startup context.
 * Blocking: must not block.
 * Ordering: completed before hrt_start(); lifecycle ordering is tightened in #33.
 */
void hrt__init_idle_task(void);

/* ---------------- Core services a port is allowed to call ----------------
 *
 * Declarations come from hardrt_kernel.h. These names form the current allowed
 * port->core surface; other kernel helpers are not part of the port contract.
 *
 * Configuration:
 *   hrt__cfg_core_hz(), hrt__cfg_tick_src(), hrt__cfg_tick_hz()
 * Tick/scheduling:
 *   hrt__tick_isr(), hrt__get_current(), hrt__set_current(),
 *   hrt__pick_next_ready(), hrt__on_scheduler_entry(), hrt__schedule()
 * Context/TCB access required by current reference ports:
 *   hrt__tcb(), _get_sp(), _set_sp(), hrt__save_current_sp(),
 *   hrt__load_next_sp_and_set_current()
 *
 * Direct TCB access is private and intentionally excluded from the installed API.
 * Further narrowing of this port-facing access is allowed without public ABI cost.
 */

#ifdef __cplusplus
}
#endif

#endif /* HARDRT_PORT_CONTRACT_INTERNAL_H */
