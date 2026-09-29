/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>

#include <trace/console_ring.h>
#include <trace/trace.h>
#include <trace/trace_archive.h>

#include "trace_stamp.h"

#define VMM_CH 1
#define UARTRX_CH 2

#define STRINGIFY(x) #x
#define VERSION_PREFIX(tag, v) tag STRINGIFY(v) " "
#define HEADER_PREFIX VERSION_PREFIX("TRH", TRACE_VERSION)
#define RECORD_PREFIX VERSION_PREFIX("TRC", TRACE_VERSION)

#define LINE_PREFIX_LEN 5
#define LINE_MAX (LINE_PREFIX_LEN + 2 * TRACE_HEADER_SIZE + 2)

uintptr_t trace_ring_vaddr;
uintptr_t console_ring_vaddr;
uintptr_t uartrx_console_ring_vaddr;
uintptr_t trace_archive_vaddr;
uintptr_t trace_control_vaddr;

static uint64_t dumps_done;

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
    emit_line(HEADER_PREFIX, &h, sizeof(h));
}

static void drain_console(uintptr_t ring_vaddr)
{
    struct console_ring *ring = (struct console_ring *)ring_vaddr;
    uint64_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);

    while (tail != __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE)) {
        microkit_dbg_putc(ring->buf[tail % CONSOLE_RING_CAPACITY]);
        tail++;
        __atomic_store_n(&ring->tail, tail, __ATOMIC_RELEASE);
    }
}

static void archive_record(const struct trace_record *rec)
{
    struct trace_archive *archive = (struct trace_archive *)trace_archive_vaddr;

    if (archive->count == TRACE_ARCHIVE_CAPACITY) {
        archive->not_archived++;
        return;
    }
    archive->records[archive->count++] = *rec;
}

static void drain_trace(void)
{
    struct trace_ring *ring = (struct trace_ring *)trace_ring_vaddr;
    uint64_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);

    /* TODO(will): scrub per-device identifiers here before observing any driver that can see them */
    while (tail != __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE)) {
        archive_record(&ring->records[tail % TRACE_RING_CAPACITY]);
        tail++;
        __atomic_store_n(&ring->tail, tail, __ATOMIC_RELEASE);
    }
}

static void put_decimal(uint64_t val)
{
    char buf[21];
    int pos = sizeof(buf) - 1;

    buf[pos] = '\0';
    do {
        buf[--pos] = (char)('0' + val % 10);
        val /= 10;
    } while (val != 0);
    microkit_dbg_puts(&buf[pos]);
}

static void dump_archive(void)
{
    struct trace_archive *archive = (struct trace_archive *)trace_archive_vaddr;
    uint64_t records_since_header = TRACE_HEADER_INTERVAL;

    for (uint64_t i = 0; i < archive->count; i++) {
        if (records_since_header == TRACE_HEADER_INTERVAL) {
            send_header();
            records_since_header = 0;
        }
        records_since_header++;
        emit_line(RECORD_PREFIX, &archive->records[i], TRACE_RECORD_SIZE);
    }
    microkit_dbg_puts("TRACER|INFO: dumped ");
    put_decimal(archive->count);
    microkit_dbg_puts(" records, ");
    put_decimal(archive->not_archived);
    microkit_dbg_puts(" not archived (archive full)\n");
}

static void maybe_dump(void)
{
    struct trace_control *control = (struct trace_control *)trace_control_vaddr;
    uint64_t requests = __atomic_load_n(&control->dump_requests, __ATOMIC_ACQUIRE);

    if (requests != dumps_done) {
        dumps_done = requests;
        dump_archive();
    }
}

void init(void)
{
}

void notified(microkit_channel ch)
{
    if (ch != VMM_CH && ch != UARTRX_CH) {
        microkit_dbg_puts("tracer: notification on unexpected channel\n");
        return;
    }
    drain_trace();
    drain_console(console_ring_vaddr);
    drain_console(uartrx_console_ring_vaddr);
    maybe_dump();
}
