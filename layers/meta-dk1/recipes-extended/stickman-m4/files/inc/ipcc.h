/* The IPCC is the hardware doorbell between the A7 and the M4. It carries no
 * data, only per-channel "occupied" flags; the payload lives in the vrings.
 * Channel numbering follows the mboxes property of the m4_rproc DT node:
 * channel 0 kicks vring0, channel 1 kicks vring1. */
#ifndef IPCC_H
#define IPCC_H

#include <stdint.h>

#define IPCC_CH_VRING0 0u
#define IPCC_CH_VRING1 1u

void ipcc_init(void);
int ipcc_rx_pending(uint32_t ch);
void ipcc_clear_rx(uint32_t ch);
void ipcc_kick(uint32_t ch);

#endif /* IPCC_H */
