#include "ipcc.h"
#include "stm32mp1xx_hal.h"

/* Polled rather than interrupt driven: the snake loop already spins, and this
 * keeps the NVIC out of the picture. */
void ipcc_init(void)
{
    __HAL_RCC_IPCC_CLK_ENABLE();
}

int ipcc_rx_pending(uint32_t ch)
{
    return (IPCC->C1TOC2SR & (1u << ch)) != 0u;
}

void ipcc_clear_rx(uint32_t ch)
{
    IPCC->C2SCR = 1u << ch;                  /* CHnC: clear the A7 -> M4 flag */
}

void ipcc_kick(uint32_t ch)
{
    /* Wait for the previous notification on this channel to be consumed,
     * otherwise setting the flag again is lost. */
    while (IPCC->C2TOC1SR & (1u << ch))
        ;
    IPCC->C2SCR = 1u << (16u + ch);          /* CHnS: set the M4 -> A7 flag */
}
