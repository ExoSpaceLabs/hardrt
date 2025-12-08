/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
/* Diagnostic HardFault trap to surface real fault context */
volatile unsigned g_cfsr, g_hfsr, g_bfar, g_afsr;
volatile unsigned g_r0,g_r1,g_r2,g_r3,g_r12,g_lr,g_pc,g_psr;
volatile uint32_t g_psp_at_fault;
volatile uint32_t g_msp_at_fault;

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4       \n"
        "ite eq           \n"
        "mrseq r0, msp    \n"
        "mrsne r0, psp    \n"
        "b hardfault_c    \n"
    );
}
void hardfault_c(unsigned *sp)
{
    volatile uint32_t *SCB_CFSR  = (uint32_t*)0xE000ED28;
    volatile uint32_t *SCB_HFSR  = (uint32_t*)0xE000ED2C;
    volatile uint32_t *SCB_BFAR  = (uint32_t*)0xE000ED38;
    volatile uint32_t *SCB_AFSR  = (uint32_t*)0xE000ED3C;

    g_r0  = sp[0]; g_r1 = sp[1]; g_r2 = sp[2]; g_r3 = sp[3];
    g_r12 = sp[4]; g_lr = sp[5]; g_pc = sp[6]; g_psr = sp[7];
    g_cfsr = *SCB_CFSR; g_hfsr = *SCB_HFSR; g_bfar = *SCB_BFAR; g_afsr = *SCB_AFSR;

    uint32_t psp, msp;
    __asm volatile ("mrs %0, psp" : "=r"(psp));
    __asm volatile ("mrs %0, msp" : "=r"(msp));
    g_psp_at_fault = psp;
    g_msp_at_fault = msp;

    for(;;){ __asm volatile("bkpt #0"); }
}
