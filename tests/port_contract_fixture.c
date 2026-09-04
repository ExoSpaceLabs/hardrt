/* Compile-only fixture for the private HardRT port contract. */
#include "hardrt_port_contract.h"

/* Required port hooks. If the authoritative prototypes drift, this fixture must
 * be updated or compilation fails. No object from this target is linked. */
void hrt_port_start_systick(uint32_t tick_hz) { (void)tick_hz; }
void hrt_port_idle_wait(void) {}
void hrt_port_enter_scheduler(void) {}
void hrt_port_yield_to_scheduler(void) {}
int hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                uint32_t *stack_base, size_t words) {
    (void)id; (void)tramp; (void)stack_base; (void)words;
    return 0;
}
void hrt_port_crit_enter(void) {}
void hrt_port_crit_exit(void) {}
void hrt_port_sp_valid(uintptr_t sp) { (void)sp; }
void hrt__pend_context_switch(void) {}
void hrt__task_trampoline(void) {}
void hrt__init_idle_task(void) {}

/* Compile-check the port->core declarations used by reference ports. */
static void hrt_port_contract_core_surface(void) {
    (void)hrt__cfg_core_hz();
    (void)hrt__cfg_tick_src();
    (void)hrt__cfg_tick_hz();
    hrt__tick_isr();
    (void)hrt__get_current();
    hrt__set_current(0);
    (void)hrt__pick_next_ready();
    hrt__on_scheduler_entry();
    (void)hrt__schedule((uintptr_t)0);
    (void)hrt__tcb(0);
    (void)_get_sp(0);
    _set_sp(0, (uint32_t *)0);
    hrt__save_current_sp((uintptr_t)0);
    (void)hrt__load_next_sp_and_set_current(0);
}

void (*const hrt_port_contract_core_surface_anchor)(void) = hrt_port_contract_core_surface;
