/* Shared-memory layout for the rpmsg vrings. These must match the
 * vdev0vring0/vdev0vring1/vdev0buffer reserved-memory nodes of the Linux
 * device tree, which are the OpenSTLinux defaults for the DK boards. */
#ifndef RSC_TABLE_H
#define RSC_TABLE_H

#include <stdint.h>

#define VRING0_DA   0x10040000u          /* M4 -> A7 */
#define VRING1_DA   0x10041000u          /* A7 -> M4 */
#define VRING_NUM   16u
#define VRING_ALIGN 16u

#define VDEV_STATUS_DRIVER_OK 4u         /* VIRTIO_CONFIG_S_DRIVER_OK */

uint8_t rsc_vdev_status(void);

#endif /* RSC_TABLE_H */
