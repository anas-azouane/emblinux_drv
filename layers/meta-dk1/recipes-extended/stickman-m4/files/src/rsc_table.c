/* Resource table for the Linux remoteproc loader.
 *
 * remoteproc parses this out of the ELF file before releasing the M4, then
 * writes the loaded copy back as the virtio handshake proceeds: negotiated
 * features and the device status land here, which is why the table is
 * volatile and why rsc_vdev_status() reads it rather than trusting the
 * initialiser.
 *
 * Two entries: the trace buffer (our log, via debugfs) and one virtio rpmsg
 * device whose vrings live in MCU SRAM.
 */
#include <stdint.h>
#include "rsc_table.h"
#include "trace.h"

#define RSC_TRACE          2u
#define RSC_VDEV           3u
#define VIRTIO_ID_RPMSG    7u
#define VIRTIO_RPMSG_F_NS  1u            /* we support the name service */

struct fw_rsc_trace {
    uint32_t type;
    uint32_t da;
    uint32_t len;
    uint32_t reserved;
    uint8_t name[32];
};

struct fw_rsc_vdev {
    uint32_t type;
    uint32_t id;
    uint32_t notifyid;
    uint32_t dfeatures;
    uint32_t gfeatures;
    uint32_t config_len;
    uint8_t status;
    uint8_t num_of_vrings;
    uint8_t reserved[2];
};

struct fw_rsc_vdev_vring {
    uint32_t da;
    uint32_t align;
    uint32_t num;
    uint32_t notifyid;
    uint32_t reserved;
};

struct resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[2];
    struct fw_rsc_trace trace;
    struct fw_rsc_vdev vdev;
    struct fw_rsc_vdev_vring vring0;
    struct fw_rsc_vdev_vring vring1;
};

volatile struct resource_table __attribute__((section(".resource_table"), used))
resource_table = {
    .ver = 1,
    .num = 2,
    .reserved = { 0, 0 },
    .offset = {
        __builtin_offsetof(struct resource_table, trace),
        __builtin_offsetof(struct resource_table, vdev),
    },
    .trace = {
        .type = RSC_TRACE,
        .da = (uint32_t)trace_buf,
        .len = TRACE_BUF_SZ,
        .reserved = 0,
        .name = "cm4_i2c_screen",
    },
    .vdev = {
        .type = RSC_VDEV,
        .id = VIRTIO_ID_RPMSG,
        .notifyid = 0,
        .dfeatures = VIRTIO_RPMSG_F_NS,
        .gfeatures = 0,
        .config_len = 0,
        .status = 0,
        .num_of_vrings = 2,
        .reserved = { 0, 0 },
    },
    .vring0 = { VRING0_DA, VRING_ALIGN, VRING_NUM, 0, 0 },
    .vring1 = { VRING1_DA, VRING_ALIGN, VRING_NUM, 1, 0 },
};

uint8_t rsc_vdev_status(void)
{
    return resource_table.vdev.status;
}
