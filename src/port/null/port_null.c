/* SPDX-License-Identifier: Apache-2.0 */
#include "heartos.h"

/* The null port just stubs required hooks so the library links.
   There’s no real tick or switching. It’s a compiler target, not a simulator. */

void hrt_port_enter_scheduler(void) {
    /* no-op in null port; hrt_start() returns immediately */
}

void hrt_port_start_systick(uint32_t tick_hz){
    (void)tick_hz;
    /* No timer. Real ports will start a tick source and call hrt__tick_isr(). */
}

void hrt_port_idle_wait(void){
    /* Nothing. Real ports could WFI or sleep. */
}

/* ISR-safe in real ports; here it's just a no-op. */
void hrt__pend_context_switch(void){
    /* Nothing. Real ports would trigger PendSV or set a rescheduling flag. */
}

/* Voluntary hop from task to scheduler. Null port doesn’t switch, so no-op. */
void hrt_port_yield_to_scheduler(void){
    /* Nothing. No scheduler to yield to. */
}

void hrt__task_trampoline(void){
    /* On real ports, this pops the initial context and jumps to task entry. */
}

/* Prepare an initial stack frame for a task.
   Null port stores nothing; core keeps metadata only. */
void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t* stack_base, size_t words)
{
    (void)id; (void)tramp; (void)stack_base; (void)words;
    /* Cortex-M port will lay out exception frame; POSIX uses ucontext. */
}
