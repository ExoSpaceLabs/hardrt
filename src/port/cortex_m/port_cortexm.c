/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include "heartos.h"
#include "heartos_time.h"

/* -------- Minimal CMSIS-like register defs (no HAL required) -------- */
#define SCS_BASE            (0xE000E000UL)
#define SysTick_BASE        (SCS_BASE + 0x0010UL)
#define SCB_BASE            (SCS_BASE + 0x0D00UL)

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
    volatile uint8_t  SHPR[12]; /* 3 x 4 bytes */
    volatile uint32_t SHCSR;
    /* ... not needed further */
} SCB_Type;

#define SysTick             ((SysTick_Type *) SysTick_BASE)
#define SCB                 ((SCB_Type *)     SCB_BASE)

/* SCB->ICSR bits */
#define SCB_ICSR_PENDSVSET_Msk   (1UL << 28)

/* SysTick->CTRL bits */
#define SYSTICK_CLKSOURCE_CPU    (1UL << 2)
#define SYSTICK_TICKINT          (1UL << 1)
#define SYSTICK_ENABLE           (1UL << 0)

/* -------- Core-private hooks we call from the port -------- */
void hrt__tick_isr(void);
void hrt__pend_context_switch(void);

/* -------- Mirror TCB so we can prep initial stack -------- */
typedef struct {
    uint32_t *sp;
    uint32_t *stack_base;
    size_t    stack_words;
    void    (*entry)(void*);
    void*     arg;
    uint32_t  wake_tick;
    uint16_t  timeslice_cfg;
    uint16_t  slice_left;
    uint8_t   prio;
    uint8_t   state;
} _hrt_tcb_t;
extern _hrt_tcb_t* hrt__tcb(int id);

/* -------- Weak app-provided core clock query --------
 * Your app should override this to return the real CPU clock (Hz).
 * We DO NOT touch clock trees here.
 */
__attribute__((weak))
uint32_t hrt_port_get_core_hz(void) {
    return 100000000u; /* 100 MHz default if user doesn't override */
}

/* -------- Critical sections via BASEPRI (M3/M4/M7) -------- */
static inline void _set_BASEPRI(uint32_t v){ __asm volatile ("msr BASEPRI, %0" :: "r"(v) : "memory"); }
static inline uint32_t _get_BASEPRI(void){ uint32_t v; __asm volatile ("mrs %0, BASEPRI" : "=r"(v)); return v; }

static uint32_t g_basepri_prev = 0;

void hrt_port_crit_enter(void){
    /* Raise BASEPRI to mask PendSV/SysTick and other preempting interrupts.
       Choose a nonzero priority level consistent with your NVIC setup. */
    uint32_t prev = _get_BASEPRI();
    /* 0x80 is a common mid-level priority mask with 8-bit fields. Adjust as needed. */
    _set_BASEPRI(0x80);
    g_basepri_prev = prev;
}

void hrt_port_crit_exit(void){
    _set_BASEPRI(g_basepri_prev);
}

/* -------- Idle wait -------- */
void hrt_port_idle_wait(void){
    __asm volatile ("wfi");
}

/* -------- Trigger a context switch (PendSV) -------- */
void hrt__pend_context_switch(void){
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk; /* set PendSV pending */
}

/* Voluntary hop: just pend a switch; handler will do the rest */
void hrt_port_yield_to_scheduler(void){
    hrt__pend_context_switch();
    /* Execution continues; switch occurs on exception return */
}

/* -------- Start SysTick at requested Hz -------- */
void hrt_port_start_systick(uint32_t tick_hz){
    if (!tick_hz) return;

    if (hrt__cfg_tick_src() == HRT_TICK_EXTERNAL) {
        // App owns a timer and will call hrt_tick_from_isr() itself.
        return;
    }

    uint32_t core_hz = hrt__cfg_core_hz();
#ifdef HEARTOS_CORE_HZ_DEFAULT
    if (!core_hz) core_hz = (uint32_t)HEARTOS_CORE_HZ_DEFAULT;
#endif
    if (!core_hz) {
        // No CPU clock info; don't start a broken SysTick.
        return;
    }

    uint32_t reload = (core_hz / tick_hz) - 1u;
    SysTick->LOAD = reload;
    SysTick->VAL  = 0;
    SCB->SHPR[11] = 0xFF;  // PendSV lowest
    SCB->SHPR[10] = 0xF0;  // SysTick low (above PendSV)
    SysTick->CTRL = SYSTICK_CLKSOURCE_CPU | SYSTICK_TICKINT | SYSTICK_ENABLE;
}

/* -------- Initial stack frame for a new task --------
 * We build the exception-return frame so that on first switch the CPU "returns"
 * into hrt__task_trampoline with LR set to task return handler.
 */
extern void hrt__task_trampoline(void);

void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t* stack_base, size_t words)
{
    (void)tramp; /* we always start via hrt__task_trampoline */
    /* ARM pushes 8 regs automatically on exception return: r0-r3,r12,lr,pc,xpsr */
    uint32_t *stk = stack_base + words;

    /* Ensure 8-byte alignment */
    stk = (uint32_t*)((uintptr_t)stk & ~0x7u);

    /* Simulate hardware-stacked frame (descending) */
    *(--stk) = 0x01000000u;                    /* xPSR: Thumb bit set */
    *(--stk) = (uint32_t)hrt__task_trampoline; /* PC */
    *(--stk) = 0xFFFFFFFDu;                    /* LR on task entry (thread, PSP) */
    *(--stk) = 0; /* r12 */
    *(--stk) = 0; /* r3  */
    *(--stk) = 0; /* r2  */
    *(--stk) = 0; /* r1  */
    /* r0 = task argument will be loaded by trampoline from TCB */

    *(--stk) = 0; /* r0 placeholder */

    /* Reserve space for callee-saved r4-r11 that PendSV saves/restores */
    for (int i = 0; i < 8; ++i) *(--stk) = 0;

    _hrt_tcb_t* t = hrt__tcb(id);
    t->sp = stk;
}

/* -------- Enter scheduler (Cortex-M flavor)
 * Nothing to loop here; we just pend the first switch and let interrupts run.
 */
void hrt_port_enter_scheduler(void){
    __asm volatile ("cpsie i"); /* enable interrupts */
    hrt__pend_context_switch();
    for(;;){
        hrt_port_idle_wait();
    }
}

/* -------- SysTick handler: tick + request reschedule -------- */
void SysTick_Handler(void){
    hrt__tick_isr();
    hrt__pend_context_switch();
}
