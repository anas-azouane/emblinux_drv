#include <stdarg.h>
#include "trace.h"

/* remoteproc reads this buffer straight out of MCU SRAM, so the firmware keeps
 * its own write index. The log stops when full rather than wrapping: a half
 * overwritten boot sequence is harder to read than a truncated one. */
char trace_buf[TRACE_BUF_SZ];
static uint32_t trace_pos;
static int trace_full;

struct sink {
    char *buf;          /* NULL for the trace buffer */
    uint32_t pos;
    uint32_t size;
};

static void emit(struct sink *s, char c)
{
    if (s->buf) {
        if (s->pos < s->size - 1) {
            s->buf[s->pos++] = c;
            s->buf[s->pos] = '\0';
        }
        return;
    }
    if (trace_pos >= TRACE_BUF_SZ - 1) {
        if (!trace_full) {
            trace_full = 1;
            const char *msg = "\n[trace buffer full]\n";
            while (*msg && trace_pos < TRACE_BUF_SZ - 1)
                trace_buf[trace_pos++] = *msg++;
        }
        return;
    }
    trace_buf[trace_pos++] = c;
    trace_buf[trace_pos] = '\0';
}

static void emit_uint(struct sink *s, uint32_t v, uint32_t base, int width, char pad)
{
    char tmp[11];
    int n = 0;

    do {
        uint32_t d = v % base;
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);

    for (int i = n; i < width; i++)
        emit(s, pad);
    while (n--)
        emit(s, tmp[n]);
}

static void format(struct sink *s, const char *fmt, va_list ap)
{
    while (*fmt) {
        if (*fmt != '%') {
            emit(s, *fmt++);
            continue;
        }
        fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        switch (*fmt++) {
        case 's': {
            const char *p = va_arg(ap, const char *);
            while (*p)
                emit(s, *p++);
            break;
        }
        case 'c': emit(s, (char)va_arg(ap, int)); break;
        case 'u': emit_uint(s, va_arg(ap, uint32_t), 10, width, '0'); break;
        case 'x':
        case 'X': emit_uint(s, va_arg(ap, uint32_t), 16, width, '0'); break;
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            if (v < 0) {
                emit(s, '-');
                v = -v;
            }
            emit_uint(s, (uint32_t)v, 10, width, '0');
            break;
        }
        case '%': emit(s, '%'); break;
        default: emit(s, '?'); break;
        }
    }
}

void trace_puts(const char *str)
{
    struct sink s = { 0 };
    while (*str)
        emit(&s, *str++);
}

void tprintf(const char *fmt, ...)
{
    struct sink s = { 0 };
    va_list ap;

    va_start(ap, fmt);
    format(&s, fmt, ap);
    va_end(ap);
}

void sfmt(char *out, uint32_t out_sz, const char *fmt, ...)
{
    struct sink s = { .buf = out, .pos = 0, .size = out_sz };
    va_list ap;

    if (!out_sz)
        return;
    out[0] = '\0';
    va_start(ap, fmt);
    format(&s, fmt, ap);
    va_end(ap);
}
