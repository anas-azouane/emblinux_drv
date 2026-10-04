/* Minimal rpmsg over the two remoteproc vrings.
 *
 * Announcing the service name "rpmsg-tty" makes the Linux rpmsg_tty driver
 * bind and create /dev/ttyRPMSG0, so anything on the A7 side can just write
 * bytes to a tty. Nothing here blocks: rpmsg_poll() is safe to call before
 * Linux has brought the virtio device up, which keeps the firmware usable
 * standalone.
 */
#ifndef RPMSG_H
#define RPMSG_H

#include <stdint.h>

/* Called from rpmsg_poll() for each message received from Linux. */
typedef void (*rpmsg_rx_cb)(const uint8_t *data, uint32_t len);

void rpmsg_init(rpmsg_rx_cb cb);
void rpmsg_poll(void);
int rpmsg_ready(void);
int rpmsg_send(const uint8_t *data, uint32_t len);

#endif /* RPMSG_H */
