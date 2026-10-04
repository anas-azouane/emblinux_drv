#include "rpmsg.h"
#include "ipcc.h"
#include "vring.h"
#include "rsc_table.h"
#include "trace.h"

#define NS_EPT        0x35u              /* reserved name-service endpoint */
#define LOCAL_EPT     0x400u             /* ours, above the reserved range */
#define SERVICE_NAME  "rpmsg-tty"        /* what rpmsg_tty binds to */

struct rpmsg_hdr {
    uint32_t src;
    uint32_t dst;
    uint32_t reserved;
    uint16_t len;
    uint16_t flags;
};

struct ns_msg {
    char name[32];
    uint32_t addr;
    uint32_t flags;                      /* 0 = RPMSG_NS_CREATE */
};

static struct vring vq_tx, vq_rx;        /* vring0 = M4 -> A7, vring1 = A7 -> M4 */
static rpmsg_rx_cb rx_cb;
static int ready;
static uint32_t peer_ept;                /* learned from the first message in */

static void *mem_copy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    while (n--)
        *d++ = *s++;
    return dst;
}

void rpmsg_init(rpmsg_rx_cb cb)
{
    rx_cb = cb;
    ipcc_init();
}

static int send_to(uint32_t dst, const uint8_t *data, uint32_t len)
{
    uint16_t head;
    uint8_t *buf;
    uint32_t cap;

    if (!ready || !vring_pop_avail(&vq_tx, &head))
        return 0;                        /* Linux has not posted a free buffer */

    vring_desc_buffer(&vq_tx, head, &buf, &cap);
    if (cap <= sizeof(struct rpmsg_hdr))
        return 0;

    uint32_t n = cap - sizeof(struct rpmsg_hdr);
    if (n > len)
        n = len;

    struct rpmsg_hdr hdr = { .src = LOCAL_EPT, .dst = dst, .reserved = 0,
                             .len = (uint16_t)n, .flags = 0 };
    mem_copy(buf, &hdr, sizeof(hdr));
    mem_copy(buf + sizeof(hdr), data, n);

    vring_push_used(&vq_tx, head, (uint32_t)(sizeof(hdr) + n));
    ipcc_kick(IPCC_CH_VRING0);
    return 1;
}

int rpmsg_send(const uint8_t *data, uint32_t len)
{
    if (!peer_ept)
        return 0;
    return send_to(peer_ept, data, len);
}

static void announce(void)
{
    struct ns_msg msg = { .addr = LOCAL_EPT, .flags = 0 };
    const char *p = SERVICE_NAME;
    uint32_t i = 0;

    while (p[i] && i < sizeof(msg.name) - 1) {
        msg.name[i] = p[i];
        i++;
    }
    msg.name[i] = '\0';

    send_to(NS_EPT, (const uint8_t *)&msg, sizeof(msg));
    tprintf("rpmsg: announced '%s' on endpoint %u\n", SERVICE_NAME, LOCAL_EPT);
}

static void drain_rx(void)
{
    uint16_t head;

    while (vring_pop_avail(&vq_rx, &head)) {
        uint8_t *buf;
        uint32_t cap;

        vring_desc_buffer(&vq_rx, head, &buf, &cap);
        if (cap >= sizeof(struct rpmsg_hdr)) {
            const struct rpmsg_hdr *hdr = (const struct rpmsg_hdr *)buf;
            uint32_t len = hdr->len;

            if (len > cap - sizeof(*hdr))
                len = cap - sizeof(*hdr);
            if (hdr->dst == LOCAL_EPT) {
                peer_ept = hdr->src;
                if (rx_cb)
                    rx_cb(buf + sizeof(*hdr), len);
            }
        }
        vring_push_used(&vq_rx, head, cap);
    }
    ipcc_kick(IPCC_CH_VRING1);
}

void rpmsg_poll(void)
{
    if (!ready) {
        /* Linux writes DRIVER_OK into the loaded resource table once its
         * virtio-rpmsg driver has set the vrings up. */
        if (!(rsc_vdev_status() & VDEV_STATUS_DRIVER_OK))
            return;

        vring_init(&vq_tx, VRING0_DA, VRING_NUM, VRING_ALIGN);
        vring_init(&vq_rx, VRING1_DA, VRING_NUM, VRING_ALIGN);
        ready = 1;
        tprintf("rpmsg: virtio driver up (status 0x%2x)\n",
                (uint32_t)rsc_vdev_status());
        announce();
        return;
    }

    if (ipcc_rx_pending(IPCC_CH_VRING1)) {
        ipcc_clear_rx(IPCC_CH_VRING1);
        drain_rx();
    }
    if (ipcc_rx_pending(IPCC_CH_VRING0))
        ipcc_clear_rx(IPCC_CH_VRING0);   /* the A7 re-posted our TX buffers */
}

int rpmsg_ready(void)
{
    return ready;
}
