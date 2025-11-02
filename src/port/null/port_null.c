/* SPDX-License-Identifier: Apache-2.0 */
#include "heartos.h"
#include "heartos_time.h"

/* Null port: provides stub hooks so the library links.
   - No tick source is started.
   - No context switching happens.
   - Useful only to compile and link the core on bare toolchains.

   Notes reflecting current core/port contract:
   - Ports should call `hrt_tick_from_isr()` from their timer ISR/thread to advance time
     and wake sleepers. Do NOT attempt to context-switch from inside the ISR.
   - Ports should implement `hrt__pend_context_switch()` as an ISR-safe request
     (e.g., set a flag or trigger a PendSV). Null port just ignores it.
   - If a port has a scheduler loop, it should call `hrt__on_scheduler_entry()`
     right after returning from a task back to the scheduler context, where it is
     safe to rotate a time-sliced task to the tail of its ready queue.
*/

void hrt_port_enter_scheduler(void) {
    /* No scheduler in null port; hrt_start() returns immediately. */
}

void hrt_port_start_systick(uint32_t tick_hz) {
    (void) tick_hz;
    /* No timer. Real ports will start a tick source and call hrt_tick_from_isr(). */
}

void hrt_port_idle_wait(void) {
    /* Nothing. Real ports could WFI or sleep. */
}

/* ISR-safe in real ports; here it's just a no-op. */
void hrt__pend_context_switch(void) {
    /* Nothing. Real ports would trigger PendSV or set a rescheduling flag. */
}

/* Voluntary hop from task to scheduler. Null port doesn’t switch, so no-op. */
void hrt_port_yield_to_scheduler(void) {
    /* Nothing. No scheduler to yield to. */
}

void hrt__task_trampoline(void) {
    /* On real ports, this pops the initial context and jumps to task entry. */
}

/* Prepare an initial stack frame for a task.
   Null port stores nothing; core keeps metadata only. */
void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t *stack_base, size_t words) {
    (void) id;
    (void) tramp;
    (void) stack_base;
    (void) words;
    /* Cortex-M port will lay out exception frame; POSIX uses ucontext. */
}


void hrt_port_crit_enter(void) {
    /* no-op */
}

void hrt_port_crit_exit(void) {
    /* no-op */
}
