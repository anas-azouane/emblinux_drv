/* Log buffer exported to Linux through the remoteproc trace resource.
 * Read it on the A7 side with:
 *   cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
 */
#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>

#define TRACE_BUF_SZ 8192

extern char trace_buf[TRACE_BUF_SZ];

void trace_puts(const char *s);

/* Both take %s %c %d %u %x %X and %%, with an optional zero-padded width. */
void tprintf(const char *fmt, ...);
void sfmt(char *out, uint32_t out_sz, const char *fmt, ...);

#endif /* TRACE_H */
