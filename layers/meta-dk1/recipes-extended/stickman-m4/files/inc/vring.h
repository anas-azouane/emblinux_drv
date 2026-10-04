/* M4-side view of a legacy virtio vring in shared MCU SRAM. Linux is the
 * driver and owns buffer allocation; we are the device, consuming available
 * descriptors and publishing them back in the used ring.
 *
 *   base + 0            desc[num], 16 bytes each
 *   base + 16*num       avail { flags, idx, ring[num], used_event }
 *   aligned up          used  { flags, idx, ring[num], avail_event }
 */
#ifndef VRING_H
#define VRING_H

#include <stdint.h>

struct vring {
    volatile uint8_t *desc;
    volatile uint16_t *avail_idx;
    volatile uint16_t *avail_ring;
    volatile uint16_t *used_idx;
    volatile uint8_t *used_ring;
    uint16_t num;
    uint16_t last_avail;
};

void vring_init(struct vring *vr, uint32_t base, uint16_t num, uint32_t align);

/* Returns 1 and sets *head when the A7 has made a descriptor available. */
int vring_pop_avail(struct vring *vr, uint16_t *head);
void vring_desc_buffer(struct vring *vr, uint16_t head, uint8_t **buf, uint32_t *len);
void vring_push_used(struct vring *vr, uint16_t head, uint32_t len);

#endif /* VRING_H */
