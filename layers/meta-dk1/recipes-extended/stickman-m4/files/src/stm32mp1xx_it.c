#include "main.h"
#include "trace.h"

void NMI_Handler(void)
{
    trace_puts("\n*** NMI ***\n");
    while (1) {}
}

/* A fault here almost always means the M4 touched a peripheral it is not
 * allowed to (ETZPC) or whose clock is gated, so the address registers are
 * worth logging before stopping. */
void HardFault_Handler(void)
{
    tprintf("\n*** HardFault: CFSR=0x%8x HFSR=0x%8x BFAR=0x%8x\n",
            SCB->CFSR, SCB->HFSR, SCB->BFAR);
    while (1) {}
}

void MemManage_Handler(void)
{
    tprintf("\n*** MemManage: CFSR=0x%8x MMFAR=0x%8x\n", SCB->CFSR, SCB->MMFAR);
    while (1) {}
}

void BusFault_Handler(void)
{
    tprintf("\n*** BusFault: CFSR=0x%8x BFAR=0x%8x\n", SCB->CFSR, SCB->BFAR);
    while (1) {}
}

void UsageFault_Handler(void)
{
    tprintf("\n*** UsageFault: CFSR=0x%8x\n", SCB->CFSR);
    while (1) {}
}

void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
