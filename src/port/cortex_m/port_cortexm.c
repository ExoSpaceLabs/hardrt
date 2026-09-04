/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>
#include "hardrt.h"
#include "hardrt_time.h"
#include "hardrt_port_contract.h"

#ifndef HARDRT_DEBUG
#define HARDRT_DEBUG 0
#endif

#if defined(__ARM_FP) && (__ARM_FP != 0)
#define HARDRT_CORTEXM_HAS_FPU 1
#else
#define HARDRT_CORTEXM_HAS_FPU 0
#endif

typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} SysTick_Type;

typedef struct {
    volatile uint32_t CPUID;
    volatile uint32_t ICSR;
    volatile uint32_t VTOR;
    volatile uint32_t AIRCR;
    volatile uint32_t SCR;
    volatile uint32_t CCR;
    volatile uint8_t SHPR[12];
    volatile uint32_t SHCSR;
} SCB_Type;

#define SCS_BASE (0xE000E000UL)
#define SysTick_BASE (SCS_BASE + 0x0010UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)

extern uint8_t __RAM_START__;
extern uint8_t __RAM_END__;
#define HRT_SP_FRAME_BYTES 64u

#define SysTick ((SysTick_Type *)SysTick_BASE)
#define SCB ((SCB_Type *)SCB_BASE)
#define SCB_ICSR_PENDSVSET_Msk (1UL << 28)
#define SYSTICK_CLKSOURCE_CPU (1UL << 2)
#define SYSTICK_TICKINT (1UL << 1)
#define SYSTICK_ENABLE (1UL << 0)

#if HARDRT_CORTEXM_HAS_FPU
#define SCB_CPACR (*(volatile uint32_t *)0xE000ED88UL)
#define FPU_FPCCR (*(volatile uint32_t *)0xE000EF34UL)
#define SCB_CPACR_CP10_CP11_FULL (0xFUL << 20)
#define FPU_FPCCR_ASPEN_Msk (1UL << 31)
#define FPU_FPCCR_LSPEN_Msk (1UL << 30)
#endif

#if HARDRT_DEBUG == 1
volatile uint32_t dbg_curr_sp;
volatile uint32_t dbg_pend_calls;
volatile uint32_t dbg_pend_from_cortexm = 0;
volatile uint32_t dbg_basperi;
#endif

static uint32_t g_idle_stack[HARDRT_IDLE_STACK_WORDS] __attribute__((aligned(8)));
static uint32_t g_systick_ctrl = 0u;

void hrt_port_sp_valid(const uintptr_t sp) {
#if HARDRT_DEBUG == 1
    dbg_curr_sp = sp;
    (void)dbg_curr_sp;
#endif
    const uintptr_t ram_lo = (uintptr_t)&__RAM_START__;
    const uintptr_t ram_hi = (uintptr_t)&__RAM_END__;
    const uintptr_t frame_bytes = HRT_SP_FRAME_BYTES;

    if (sp == 0u) hrt_error(ERR_SP_NULL);
    if (ram_hi - ram_lo < 2u * frame_bytes) hrt_error(ERR_INVALID_RAM_RANGE);
    if (sp < ram_lo + frame_bytes || sp > ram_hi - frame_bytes) hrt_error(ERR_STACK_RANGE);
#if HARDRT_CORTEXM_HAS_FPU
    /* Saving EXC_RETURN adds one word to the software frame. The stored TCB SP
       can therefore be 4-byte aligned even though exception return restores
       the architectural PSP to the original 8-byte-aligned frame boundary. */
    if (sp & 0x3u) hrt_error(ERR_STACK_ALIGN);
#else
    if (sp & 0x7u) hrt_error(ERR_STACK_ALIGN);
#endif
}

__attribute__((weak))
uint32_t hrt_port_get_core_hz(void) {
    extern uint32_t SystemCoreClock;
    if (SystemCoreClock) return SystemCoreClock;
    return 100000000u;
}

#ifndef HARDRT_NVIC_PRIO_BITS
#define HARDRT_NVIC_PRIO_BITS 4u
#endif

#ifndef HARDRT_MAX_SYSCALL_IRQ_PRIO
#define HARDRT_MAX_SYSCALL_IRQ_PRIO 5u
#endif

static inline uint32_t _prio_to_basepri(uint32_t prio) {
    return (prio << (8u - (uint32_t)HARDRT_NVIC_PRIO_BITS)) & 0xFFu;
}

static inline void _set_BASEPRI(uint32_t v) {
    __asm volatile ("msr BASEPRI, %0" :: "r"(v) : "memory");
}

static inline uint32_t _get_BASEPRI(void) {
    uint32_t v;
    __asm volatile ("mrs %0, BASEPRI" : "=r"(v));
    return v;
}

static inline void _hrt_port_barrier(void) {
    __asm volatile ("dsb 0xF" ::: "memory");
    __asm volatile ("isb 0xF" ::: "memory");
}

#if HARDRT_CORTEXM_HAS_FPU
static void _configure_fpu_context(void) {
    /* The qualified hard-float Cortex-M contract owns task FP context.
       Enable CP10/CP11 and architectural automatic/lazy FP state preservation
       before the first HardRT task can execute floating-point instructions. */
    SCB_CPACR |= SCB_CPACR_CP10_CP11_FULL;
    _hrt_port_barrier();
    FPU_FPCCR |= FPU_FPCCR_ASPEN_Msk | FPU_FPCCR_LSPEN_Msk;
    _hrt_port_barrier();
}
#endif

static uint32_t g_basepri_prev = 0;
static volatile uint32_t g_cs_nest = 0;

void hrt_port_crit_enter(void) {
    const uint32_t prev = _get_BASEPRI();
    if (g_cs_nest == 0u) {
        g_basepri_prev = prev;
        _set_BASEPRI(_prio_to_basepri(HARDRT_MAX_SYSCALL_IRQ_PRIO));
        _hrt_port_barrier();
    }
    g_cs_nest++;
}

void hrt_port_crit_exit(void) {
    if (g_cs_nest == 0u) return;
    g_cs_nest--;
    if (g_cs_nest == 0u) {
        _set_BASEPRI(g_basepri_prev);
        _hrt_port_barrier();
    }
}

void hrt_port_idle_wait(void) {
    __asm volatile ("wfi");
}

static void hrt_idle_task(void *arg) {
    (void)arg;
    for (;;) hrt_port_idle_wait();
}

void hrt__init_idle_task(void) {
    _hrt_tcb_t *idle = hrt__tcb(HRT_IDLE_ID);
    if (idle == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }

    idle->state = HRT_READY;
    idle->prio = 0u;
    idle->timeslice_cfg = 0u;
    idle->slice_left = 0u;
    idle->entry = hrt_idle_task;
    idle->arg = NULL;
    idle->stack_base = g_idle_stack;
    idle->stack_words = HARDRT_IDLE_STACK_WORDS;

    uint32_t *sp = &g_idle_stack[HARDRT_IDLE_STACK_WORDS];
    *(--sp) = 0x01000000u;
    *(--sp) = (uint32_t)hrt_idle_task;
    *(--sp) = (uint32_t)hrt__task_trampoline;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
#if HARDRT_CORTEXM_HAS_FPU
    /* Software EXC_RETURN accompanies r4-r11 in FP-capable builds. New/idle
       contexts start basic and become extended only after Thread mode actually
       activates the FPU. */
    *(--sp) = 0xFFFFFFFDu;
#endif
    for (int i = 0; i < 8; ++i) *(--sp) = 0;

    _set_sp(HRT_IDLE_ID, sp);
}

static inline void _pend_pendsv(void) {
#if HARDRT_DEBUG == 1
    dbg_basperi = _get_BASEPRI();
    dbg_pend_calls++;
#endif
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    _hrt_port_barrier();
}

void hrt__pend_context_switch(void) {
    _pend_pendsv();
}

void hrt_port_yield_to_scheduler(void) {
    /* Core task-context paths follow a two-stage contract:
     *   hrt__pend_context_switch();
     *   hrt_port_yield_to_scheduler();
     *
     * On POSIX the first stage sets a pending flag and the second performs the
     * host context hop. On Cortex-M, however, the first stage already requests
     * PendSV and the DSB/ISB in _pend_pendsv() makes that request visible at the
     * architectural exception boundary. Re-pending here can make a task that
     * has just resumed from a blocking call enter PendSV a second time before
     * the API returns. The task-context yield stage therefore has no additional
     * hardware action on Cortex-M.
     */
}

int hrt_port_configure_tick(const uint32_t tick_hz) {
    g_systick_ctrl = 0u;

    /* PendSV is part of the scheduler mechanism in both tick modes. Merely
       configure its priority here; hrt_init() must not enable interrupts. */
    SCB->SHPR[10] = 0xF0;

    if (hrt__cfg_tick_src() == HRT_TICK_EXTERNAL) return 0;
    if (tick_hz == 0u) return -1;

    const uint32_t core_hz = hrt_port_get_core_hz();
    if (core_hz == 0u) return -1;

    const uint32_t counts = core_hz / tick_hz;
    if (counts == 0u || counts > 0x01000000u) return -1;

    /* Configure but deliberately leave SysTick disabled until hrt_start(). */
    SysTick->CTRL = 0u;
    SysTick->LOAD = counts - 1u;
    SysTick->VAL = 0u;
    SCB->SHPR[11] = 0xE0;
    g_systick_ctrl = SYSTICK_CLKSOURCE_CPU | SYSTICK_TICKINT | SYSTICK_ENABLE;
    _hrt_port_barrier();
    return 0;
}

int hrt_port_prepare_task_stack(const int id, void (*tramp)(void),
                                uint32_t *stack_base, const size_t words) {
    (void)tramp;
    uint32_t *stack_end = stack_base + words;
    uint32_t *stk = (uint32_t *)((uintptr_t)stack_end & ~(uintptr_t)0x7u);

    if (stk <= stack_base || stk > stack_end) {
        hrt_error(ERR_STACK_RANGE);
        return -1;
    }

    *(--stk) = 0x01000000u;
    *(--stk) = (uint32_t)hrt__task_trampoline;
    *(--stk) = 0xFFFFFFFDu;
    *(--stk) = 0;
    *(--stk) = 0;
    *(--stk) = 0;
    *(--stk) = 0;
    *(--stk) = 0;
#if HARDRT_CORTEXM_HAS_FPU
    *(--stk) = 0xFFFFFFFDu;
#endif
    for (int i = 0; i < 8; ++i) *(--stk) = 0;

    if (stk < stack_base) {
        hrt_error(ERR_STACK_UNDERFLOW_INIT);
        return -1;
    }
    _set_sp(id, stk);
    return 0;
}

void hrt_port_enter_scheduler(void) {
    /* Scheduler startup is one ordered boundary: no task or tick can run until
       FP context support is configured, the first PendSV is pending, and the
       selected periodic tick is armed. */
    __asm volatile ("cpsid i");
#if HARDRT_CORTEXM_HAS_FPU
    _configure_fpu_context();
#endif
#if HARDRT_DEBUG == 1
    dbg_pend_from_cortexm++;
#endif
    hrt__pend_context_switch();

    if (hrt__cfg_tick_src() != HRT_TICK_EXTERNAL) {
        SysTick->VAL = 0u;
        SysTick->CTRL = g_systick_ctrl;
        _hrt_port_barrier();
    }

    __asm volatile ("cpsie i");
    for (;;) hrt_port_idle_wait();
}

void SysTick_Handler(void) {
    if (hrt__cfg_tick_src() != HRT_TICK_EXTERNAL) hrt__tick_isr();
}
