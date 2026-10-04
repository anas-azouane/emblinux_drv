#include "vring.h"

#define DESC_SZ 16                       /* addr(8) len(4) flags(2) next(2) */
#define USED_SZ 8                        /* id(4) len(4) */

static void dmb(void)
{
    __asm volatile ("dmb" ::: "memory");
}

void vring_init(struct vring *vr, uint32_t base, uint16_t num, uint32_t align)
{
    uint32_t avail = base + (uint32_t)num * DESC_SZ;
    uint32_t avail_end = avail + 2u * (3u + num);       /* flags, idx, ring, event */
    uint32_t used = (avail_end + align - 1u) & ~(align - 1u);

    vr->desc = (volatile uint8_t *)base;
    vr->avail_idx = (volatile uint16_t *)(avail + 2u);
    vr->avail_ring = (volatile uint16_t *)(avail + 4u);
    vr->used_idx = (volatile uint16_t *)(used + 2u);
    vr->used_ring = (volatile uint8_t *)(used + 4u);
    vr->num = num;
    vr->last_avail = 0;
}

int vring_pop_avail(struct vring *vr, uint16_t *head)
{
    if (*vr->avail_idx == vr->last_avail)
        return 0;

    dmb();                               /* read the ring after the index */
    *head = vr->avail_ring[vr->last_avail % vr->num];
    vr->last_avail++;
    return 1;
}

/* rpmsg always uses single-descriptor chains, so the next field is ignored. */
void vring_desc_buffer(struct vring *vr, uint16_t head, uint8_t **buf, uint32_t *len)
{
    volatile uint32_t *d = (volatile uint32_t *)(vr->desc + (head % vr->num) * DESC_SZ);

    *buf = (uint8_t *)d[0];              /* low half of the 64-bit address */
    *len = d[2];
}

void vring_push_used(struct vring *vr, uint16_t head, uint32_t len)
{
    uint16_t idx = *vr->used_idx;
    volatile uint32_t *elem =
        (volatile uint32_t *)(vr->used_ring + (idx % vr->num) * USED_SZ);

    elem[0] = head;
    elem[1] = len;
    dmb();                               /* publish the element before the index */
    *vr->used_idx = (uint16_t)(idx + 1u);
}
