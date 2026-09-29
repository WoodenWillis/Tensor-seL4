/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

/* docs/trace-format.md, version 0 */
#define TRACE_V0_VERSION            0
#define TRACE_V0_MAGIC              "SEL4TRC"
#define TRACE_V0_RECORD_SIZE        64
#define TRACE_V0_HEADER_SIZE        336
#define TRACE_V0_RING_SIZE          0x10000
#define TRACE_V0_RING_HEADER_SIZE   64
#define TRACE_V0_RING_CAPACITY      ((TRACE_V0_RING_SIZE - TRACE_V0_RING_HEADER_SIZE) / TRACE_V0_RECORD_SIZE)

#define TRACE_KIND_MMIO             1

#define TRACE_MMIO_WRITE            (1u << 0)
#define TRACE_MMIO_FORWARDED        (1u << 1)
#define TRACE_MMIO_UNHANDLED        (1u << 2)

struct trace_record_v0 {
    uint64_t seq;
    uint64_t time;
    uint64_t pc;
    uint64_t addr;
    uint64_t value;
    uint32_t esr;
    uint16_t vcpu;
    uint8_t producer;
    uint8_t kind;
    uint8_t size;
    uint8_t flags;
    uint8_t reserved0[6];
    uint64_t reserved1;
};

_Static_assert(sizeof(struct trace_record_v0) == TRACE_V0_RECORD_SIZE, "trace v0 record is 64 bytes");

struct trace_header_v0 {
    char magic[8];
    uint16_t version;
    uint16_t record_size;
    uint32_t cntfrq;
    char codename[16];
    char build_id[32];
    char bootloader[48];
    char baseband[48];
    char git_sha[48];
    char toolchain_sha256[64];
    char dtb_sha256[64];
};

_Static_assert(sizeof(struct trace_header_v0) == TRACE_V0_HEADER_SIZE, "trace v0 header is 336 bytes");

struct trace_ring_v0 {
    uint64_t head;
    uint64_t tail;
    uint64_t next_seq;
    uint8_t reserved[TRACE_V0_RING_HEADER_SIZE - 3 * sizeof(uint64_t)];
    struct trace_record_v0 records[TRACE_V0_RING_CAPACITY];
};

_Static_assert(sizeof(struct trace_ring_v0) <= TRACE_V0_RING_SIZE, "trace v0 ring fits its memory region");
