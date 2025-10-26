#include "heartos.h"
#include <stdio.h>

/* The null port just stubs required hooks so the library links.
   There’s no real tick or switching. It’s a compile target, not a simulator. */

void hrt_port_start_systick(uint32_t tick_hz){
    (void)tick_hz;
    /* No timer. Real ports will start a tick source and call hrt__tick_isr(). */
}

void hrt_port_idle_wait(void){
    /* Nothing. Real ports could WFI or sleep. */
}

void hrt__pend_context_switch(void){
    /* Nothing. Real ports trigger PendSV or swap contexts. */
}

void hrt__task_trampoline(void){
    /* On real ports, this pops initial context and jumps to task entry. */
}

/* Prepare initial stack frame for a task.
   Null port stores nothing; core keeps metadata only. */
void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t* stack_base, size_t words)
{
    (void)id; (void)tramp; (void)stack_base; (void)words;
    /* Cortex-M port will lay out exception frame here. POSIX port will set ucontext. */
}
