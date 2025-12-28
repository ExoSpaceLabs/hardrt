/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>
#include "hardrt.h"
#include "hardrt_time.h"
#include "hardrt_port_int.h"

#ifndef HARDRT_DEBUG
    #define HARDRT_BDG_VARIABLES 0
    #define HARDRT_VALIDATION 0
#else
    #define HARDRT_BDG_VARIABLES 1
    #define HARDRT_VALIDATION 1
#endif

/* -------- Minimal CMSIS-like register defs (no HAL required) -------- */

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
    volatile uint8_t  SHPR[12]; /* system handler priority registers */
    volatile uint32_t SHCSR;
} SCB_Type;

#define SCS_BASE            (0xE000E000UL)
#define SysTick_BASE        (SCS_BASE + 0x0010UL)
#define SCB_BASE            (SCS_BASE + 0x0D00UL)

// variables defined in the linker script that show the address limits of RAM.
extern uint8_t __RAM_START__;
extern uint8_t __RAM_END__;
#define HRT_SP_FRAME_BYTES 64u

#define SysTick             ((SysTick_Type *) SysTick_BASE)
#define SCB                 ((SCB_Type *)     SCB_BASE)

/* SCB->ICSR bits */
#define SCB_ICSR_PENDSVSET_Msk   (1UL << 28)

/* SysTick->CTRL bits */
#define SYSTICK_CLKSOURCE_CPU    (1UL << 2)
#define SYSTICK_TICKINT          (1UL << 1)
#define SYSTICK_ENABLE           (1UL << 0)

/* debug variables */
#if HARDRT_BDG_VARIABLES == 1
volatile uint32_t dbg_curr_sp;
volatile uint32_t dbg_pend_calls;
volatile uint32_t dbg_pend_from_cortexm = 0;
volatile uint32_t dbg_basperi;
#endif


/* -------- Core-private hooks we call from the port -------- */
extern void hrt__tick_isr(void);
extern _hrt_tcb_t* hrt__tcb(int id);

void hrt_port_sp_valid(const uint32_t sp) {
#if HARDRT_BDG_VARIABLES == 1
    dbg_curr_sp = sp;
    (void)dbg_curr_sp;
#endif
    const uint32_t ram_lo = (uintptr_t)&__RAM_START__;
    const uint32_t ram_hi = (uintptr_t)&__RAM_END__;
    const uint32_t frame_bytes = HRT_SP_FRAME_BYTES;
    if (sp == 0) {
        hrt_error(ERR_SP_NULL);
    }
    if (ram_hi - ram_lo < 2 * frame_bytes) {
        hrt_error(ERR_INVALID_RAM_RANGE);
    }

    /* basic range guard */
    if (sp < ram_lo + frame_bytes || sp > ram_hi - frame_bytes) {
        hrt_error(ERR_STACK_RANGE);
    }

    /* AAPCS: 8-byte alignment for SP */
    if (sp & 0x7u) {
        hrt_error(ERR_STACK_ALIGN);
    };
}
/* -------- Core clock query (weak) --------
 * Prefer SystemCoreClock if the vendor file is linked; otherwise fallback.
 * If you REALLY want to drive this via hrt_config_t, provide a strong
 * app-level override of this function that returns cfg->core_hz.
 */
__attribute__((weak))
uint32_t hrt_port_get_core_hz(void) {
    extern uint32_t SystemCoreClock; /* provided by system_stm32h7xx.c */
    if (SystemCoreClock) return SystemCoreClock;
    return 100000000u; /* fallback: 100 MHz */
}

/* -------- Critical sections via BASEPRI (M3/M4/M7) -------- */
#ifndef HARDRT_NVIC_PRIO_BITS
#define HARDRT_NVIC_PRIO_BITS 4u
#endif

#ifndef HARDRT_MAX_SYSCALL_IRQ_PRIO
#define HARDRT_MAX_SYSCALL_IRQ_PRIO  5u
#endif

static inline uint32_t _prio_to_basepri(uint32_t prio)
{
    /* Convert logical priority (0.(2^__NVIC_PRIO_BITS-1)) to 8-bit field */
    return (prio << (8u - (uint32_t)HARDRT_NVIC_PRIO_BITS)) & 0xFFu;
}
static inline void _set_BASEPRI(uint32_t v){ __asm volatile ("msr BASEPRI, %0" :: "r"(v) : "memory"); }
static inline uint32_t _get_BASEPRI(void){ uint32_t v; __asm volatile ("mrs %0, BASEPRI" : "=r"(v)); return v; }

static inline void _hrt_port_barrier(void) {
    __asm volatile ("dsb 0xF" ::: "memory");
    __asm volatile ("isb 0xF" ::: "memory");
}


static uint32_t g_basepri_prev = 0;
static volatile uint32_t g_cs_nest = 0; /* critical section nesting counter */

void hrt_port_crit_enter(void){
    /* Mask preempting interrupts (including SysTick/PendSV). Priority 0x80 is a sane mid level. */
    const uint32_t prev = _get_BASEPRI();
    if (g_cs_nest == 0u) {
        g_basepri_prev = prev;            /* save only on outermost enter */
        _set_BASEPRI(_prio_to_basepri(HARDRT_MAX_SYSCALL_IRQ_PRIO));               /* raise BASEPRI threshold */
        _hrt_port_barrier();
    }
    g_cs_nest++;
}

void hrt_port_crit_exit(void){
    if (g_cs_nest == 0u) {
        /* underflow guard: nothing to do */
        return;
    }
    g_cs_nest--;
    if (g_cs_nest == 0u) {
        /* restore previous threshold on outermost exit */
        _set_BASEPRI(g_basepri_prev);
        //__asm volatile("dsb sy\nisb");
        _hrt_port_barrier();
    }
}

/* -------- Idle wait -------- */
void hrt_port_idle_wait(void){
    __asm volatile ("wfi");
}

// idle task for when tasks are not available
static void hrt_idle_task(void *arg) {
    (void)arg;
    for (;;) {
        hrt_port_idle_wait();
    }
}

void hrt__init_idle_task(void)
{
    g_idle_tcb = (_hrt_tcb_t){0};

    g_idle_tcb.state         = HRT_READY;        // always logically runnable
    g_idle_tcb.prio          = 0;                // irrelevant if not in rq
    g_idle_tcb.timeslice_cfg = 0;                // cooperative, never RR
    g_idle_tcb.slice_left    = 0;
    g_idle_tcb.entry         = hrt_idle_task;
    g_idle_tcb.arg           = NULL;

    // Build initial stack frame exactly like hrt_create_task does:
    uint32_t *sp = &g_idle_stack[HARDRT_IDLE_STACK_WORDS];

    // Auto-stacked frame (xPSR, PC, LR, R12, R3, R2, R1, R0):
    *(--sp) = 0x01000000u;                 // xPSR (Thumb bit set)
    *(--sp) = (uint32_t)hrt_idle_task;     // PC
    *(--sp) = (uint32_t)hrt__task_trampoline; // LR on task exit (never reached)
    *(--sp) = 0; // R12
    *(--sp) = 0; // R3
    *(--sp) = 0; // R2
    *(--sp) = 0; // R1
    *(--sp) = 0; // R0 (arg)

    // Then the "manually" saved r4–r11, initialized to 0:
    for (int i = 0; i < 8; ++i) {
        *(--sp) = 0;
    }

    g_idle_tcb.sp = sp;
    _set_sp(HRT_IDLE_ID, sp);     // whatever your internal helper is
}


/* -------- Trigger a context switch (PendSV) -------- */
static inline void _pend_pendsv(void){
#if HARDRT_BDG_VARIABLES == 1
    dbg_basperi = _get_BASEPRI();
    dbg_pend_calls++;
#endif
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk; /* set PendSV pending */
    _hrt_port_barrier();
}

void hrt__pend_context_switch(void){
    _pend_pendsv();
}

/* Voluntary hop: just pend a switch; handler will do the rest on exception return */
void hrt_port_yield_to_scheduler(void){
    _pend_pendsv();
    /* execution continues; the switch happens at exception return */
}

/* -------- Start SysTick at requested Hz -------- */
void hrt_port_start_systick(uint32_t tick_hz){
    if (!tick_hz) return;
    /* If EXTERNAL tick mode, bypass here in that case. */
    if (hrt__cfg_tick_src() == HRT_TICK_EXTERNAL) {
        SCB->SHPR[10] = 0xF0; /* PendSV lowest (effective on CM7) */
        __asm volatile ("cpsie i");
        return;
    }

    const uint32_t core_hz = hrt_port_get_core_hz();
    if (!core_hz) return;

    uint32_t reload = core_hz / tick_hz;
    if (reload == 0u) reload = 1u;
    if (reload > 0xFFFFFFu) reload = 0xFFFFFFu;
    reload -= 1u;

    /* Program SysTick */
    SysTick->LOAD = reload;
    SysTick->VAL  = 0;

    /* Priority hygiene: PendSV lowest (0xFF), SysTick just above (0xFE).
       On CM7 with 4-bit implemented priority, only upper bits count.
       SHPR[10] byte 2 maps PendSV, SHPR[11] byte 3 maps SysTick in ARMv7-M. */
    SCB->SHPR[10] = 0xF0; /* PendSV lowest (effective on CM7) */
    SCB->SHPR[11] = 0xE0; /* SysTick higher than PendSV (effective on CM7) */

    SysTick->CTRL = SYSTICK_CLKSOURCE_CPU | SYSTICK_TICKINT | SYSTICK_ENABLE;

    /* Make sure global interrupts are enabled */
    __asm volatile ("cpsie i");
}

/* -------- Initial stack frame for a new task --------
 * Build the exception-return frame so the first switch "returns"
 * into hrt__task_trampoline with Thread mode/PSP.
 */
extern void hrt__task_trampoline(void);

void hrt_port_prepare_task_stack(const int id, void (*tramp)(void),
                                 uint32_t* stack_base, const size_t words)
{
    (void)tramp;
    uint32_t *stack_end = stack_base + words;
    uint32_t *stk = stack_end;

    stk = (uint32_t*)((uintptr_t)stk & ~0x7u);

    // guard: stack must remain in [stack_base, stack_end]
    if (stk <= stack_base || stk > stack_end) {
        hrt_error(ERR_STACK_RANGE);
        return;
    }

    *(--stk) = 0x01000000u;
    *(--stk) = (uint32_t)hrt__task_trampoline;
    *(--stk) = 0xFFFFFFFDu;
    *(--stk) = 0;
    *(--stk) = 0;
    *(--stk) = 0;
    *(--stk) = 0;
    *(--stk) = 0;

    for (int i = 0; i < 8; ++i) *(--stk) = 0;

    if (stk < stack_base) {
        hrt_error(ERR_STACK_UNDERFLOW_INIT);
        return;
    }

    _set_sp(id, stk);
}

/* -------- Enter scheduler (Cortex-M flavor)
 * Enable IRQs, pend the first switch, then idle forever.
 */
void hrt_port_enter_scheduler(void){

    __asm volatile ("cpsie i");
#if HARDRT_BDG_VARIABLES == 1
    dbg_pend_from_cortexm++;
#endif
    hrt__pend_context_switch();
    for(;;) { hrt_port_idle_wait(); }
}

/* -------- SysTick handler: tick + request reschedule -------- */
void SysTick_Handler(void){
    if (hrt__cfg_tick_src() != HRT_TICK_EXTERNAL) {
        hrt__tick_isr();
    }
}
