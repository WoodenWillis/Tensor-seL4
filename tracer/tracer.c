/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>

#include <trace/console_ring.h>
#include <trace/trace.h>

#include "trace_stamp.h"

#define PRODUCER_CH 1

#define LINE_PREFIX_LEN 5
#define LINE_MAX (LINE_PREFIX_LEN + 2 * TRACE_HEADER_SIZE + 2)

uintptr_t trace_ring_vaddr;
uintptr_t console_ring_vaddr;

static bool header_sent;
static char line[LINE_MAX];

static uint32_t read_cntfrq(void)
{
    uint64_t val;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return (uint32_t)val;
}

static void copy_field(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;

    for (; i < dst_len && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < dst_len; i++) {
        dst[i] = '\0';
    }
}

static void emit_line(const char prefix[LINE_PREFIX_LEN + 1], const void *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    const uint8_t *bytes = data;
    size_t pos = 0;

    for (size_t i = 0; i < LINE_PREFIX_LEN; i++) {
        line[pos++] = prefix[i];
    }
    for (size_t i = 0; i < len; i++) {
        line[pos++] = hex[bytes[i] >> 4];
        line[pos++] = hex[bytes[i] & 0xf];
    }
    line[pos++] = '\n';
    line[pos] = '\0';
    microkit_dbg_puts(line);
}

static void send_header(void)
{
    struct trace_header h;

    copy_field(h.magic, sizeof(h.magic), TRACE_MAGIC);
    h.version = TRACE_VERSION;
    h.record_size = TRACE_RECORD_SIZE;
    h.cntfrq = read_cntfrq();
    copy_field(h.codename, sizeof(h.codename), TRACE_STAMP_CODENAME);
    copy_field(h.build_id, sizeof(h.build_id), TRACE_STAMP_BUILD_ID);
    copy_field(h.bootloader, sizeof(h.bootloader), TRACE_STAMP_BOOTLOADER);
    copy_field(h.baseband, sizeof(h.baseband), TRACE_STAMP_BASEBAND);
    copy_field(h.git_sha, sizeof(h.git_sha), TRACE_STAMP_GIT_SHA);
    copy_field(h.toolchain_sha256, sizeof(h.toolchain_sha256), TRACE_STAMP_TOOLCHAIN_SHA256);
    copy_field(h.dtb_sha256, sizeof(h.dtb_sha256), TRACE_STAMP_DTB_SHA256);
    emit_line("TRH1 ", &h, sizeof(h));
}

static void drain_console(void)
{
    struct console_ring *ring = (struct console_ring *)console_ring_vaddr;
    uint64_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);

    while (tail != __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE)) {
        microkit_dbg_putc(ring->buf[tail % CONSOLE_RING_CAPACITY]);
        tail++;
        __atomic_store_n(&ring->tail, tail, __ATOMIC_RELEASE);
    }
}

static void drain_trace(void)
{
    struct trace_ring *ring = (struct trace_ring *)trace_ring_vaddr;
    uint64_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);

    /* TODO(will): scrub per-device identifiers here before observing any driver that can see them */
    while (tail != __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE)) {
        emit_line("TRC1 ", &ring->records[tail % TRACE_RING_CAPACITY], TRACE_RECORD_SIZE);
        tail++;
        __atomic_store_n(&ring->tail, tail, __ATOMIC_RELEASE);
    }
}

void init(void)
{
}

void notified(microkit_channel ch)
{
    if (ch != PRODUCER_CH) {
        microkit_dbg_puts("tracer: notification on unexpected channel\n");
        return;
    }
    if (!header_sent) {
        send_header();
        header_sent = true;
    }
    drain_trace();
    drain_console();
}
