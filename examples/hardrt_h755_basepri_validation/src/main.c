#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx.h"

/* Private Cortex-M port hooks under qualification. They are intentionally not
 * part of the installed application API; this fixture links against the
 * reference port specifically to validate its interrupt-mask contract. */
extern void hrt_port_crit_enter(void);
extern void hrt_port_crit_exit(void);

typedef struct {
    uint32_t passed;
    uint32_t error;
    uint32_t priority_step;
    uint32_t hardrt_mask;
    uint32_t zero_inside;
    uint32_t zero_after;
    uint32_t weaker_before;
    uint32_t weaker_inside;
    uint32_t weaker_nested;
    uint32_t weaker_after_inner;
    uint32_t weaker_after_outer;
    uint32_t stricter_before;
    uint32_t stricter_inside;
    uint32_t stricter_nested;
    uint32_t stricter_after_inner;
    uint32_t stricter_after_outer;
    uint32_t final_basepri;
} hrt_basepri_result_t;

_Static_assert(offsetof(hrt_basepri_result_t, passed) == 0u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, hardrt_mask) == 12u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, weaker_before) == 24u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, stricter_before) == 44u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, final_basepri) == 64u, "BASEPRI result ABI");
_Static_assert(sizeof(hrt_basepri_result_t) == 68u, "BASEPRI result ABI");

static volatile hrt_basepri_result_t g_result;

extern void SystemInit(void);

static inline void barrier(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
}

static inline uint32_t get_basepri(void)
{
    uint32_t value;
    __asm volatile("mrs %0, BASEPRI" : "=r"(value));
    return value;
}

static inline void set_basepri(uint32_t value)
{
    __asm volatile("msr BASEPRI, %0" :: "r"(value) : "memory");
    barrier();
}

__attribute__((noinline, used))
void basepri_validation_emit(const volatile hrt_basepri_result_t *result)
{
    __asm volatile("" : : "r"(result) : "memory");
    for (;;) __asm volatile("wfi");
}

static void stop_validation(uint32_t error, uint32_t passed)
{
    g_result.error = error;
    g_result.passed = passed;
    g_result.final_basepri = get_basepri();
    basepri_validation_emit(&g_result);
}

static void fail(uint32_t error)
{
    stop_validation(error, 0u);
}

static void pass(void)
{
    set_basepri(0u);
    g_result.final_basepri = get_basepri();
    stop_validation(0u, 1u);
}

static inline void hold_cm4(void)
{
#define RCC_BASE_NEW   0x58024400UL
#define RCC_GCR        (*(volatile uint32_t *)(RCC_BASE_NEW + 0x0u))
#define RCC_GRSTCSETR  (*(volatile uint32_t *)(RCC_BASE_NEW + 0x8u))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = (1u << 0);
}

int main(void)
{
    SystemInit();
    hold_cm4();

#ifndef __NVIC_PRIO_BITS
#error "CMSIS __NVIC_PRIO_BITS is required for BASEPRI validation"
#endif

    const uint32_t priority_step = 1u << (8u - (uint32_t)__NVIC_PRIO_BITS);
    g_result.priority_step = priority_step;

    /* Case 1: from an unmasked caller, HardRT must install its syscall ceiling
     * and then restore the exact zero state on exit. */
    set_basepri(0u);
    if (get_basepri() != 0u) fail(1u);

    hrt_port_crit_enter();
    g_result.zero_inside = get_basepri();
    g_result.hardrt_mask = g_result.zero_inside;
    if (g_result.hardrt_mask == 0u) fail(2u);
    hrt_port_crit_exit();

    g_result.zero_after = get_basepri();
    if (g_result.zero_after != 0u) fail(3u);

    const uint32_t hardrt_mask = g_result.hardrt_mask;
    if (hardrt_mask <= priority_step || hardrt_mask > (0xFFu - priority_step)) fail(4u);

    /* Case 2: a weaker pre-existing mask must be raised to HardRT's stricter
     * ceiling for the outer critical section, remain stable through nesting,
     * then restore exactly to the weaker pre-entry value. */
    const uint32_t weaker = hardrt_mask + priority_step;
    set_basepri(weaker);
    g_result.weaker_before = get_basepri();
    if (g_result.weaker_before != weaker) fail(5u);

    hrt_port_crit_enter();
    g_result.weaker_inside = get_basepri();
    if (g_result.weaker_inside != hardrt_mask) fail(6u);

    hrt_port_crit_enter();
    g_result.weaker_nested = get_basepri();
    if (g_result.weaker_nested != hardrt_mask) fail(7u);

    hrt_port_crit_exit();
    g_result.weaker_after_inner = get_basepri();
    if (g_result.weaker_after_inner != hardrt_mask) fail(8u);

    hrt_port_crit_exit();
    g_result.weaker_after_outer = get_basepri();
    if (g_result.weaker_after_outer != weaker) fail(9u);

    /* Case 3: a stricter pre-existing mask must never be weakened. This is the
     * regression that fails with an unconditional `msr BASEPRI, hardrt_mask`.
     * Nested entry/exit must preserve it, and outer exit must restore it exactly. */
    const uint32_t stricter = hardrt_mask - priority_step;
    set_basepri(stricter);
    g_result.stricter_before = get_basepri();
    if (g_result.stricter_before != stricter) fail(10u);

    hrt_port_crit_enter();
    g_result.stricter_inside = get_basepri();
    if (g_result.stricter_inside != stricter) fail(11u);

    hrt_port_crit_enter();
    g_result.stricter_nested = get_basepri();
    if (g_result.stricter_nested != stricter) fail(12u);

    hrt_port_crit_exit();
    g_result.stricter_after_inner = get_basepri();
    if (g_result.stricter_after_inner != stricter) fail(13u);

    hrt_port_crit_exit();
    g_result.stricter_after_outer = get_basepri();
    if (g_result.stricter_after_outer != stricter) fail(14u);

    pass();
    return 0;
}
