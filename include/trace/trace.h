/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

/* docs/trace-format.md, version 3 */
#define TRACE_VERSION               5
#define TRACE_MAGIC                 "SEL4TRC"
#define TRACE_RECORD_SIZE           64
#define TRACE_HEADER_SIZE           336
#define TRACE_HEADER_INTERVAL       64
#define TRACE_RING_SIZE             0x10000
#define TRACE_RING_HEADER_SIZE      64
#define TRACE_RING_CAPACITY         ((TRACE_RING_SIZE - TRACE_RING_HEADER_SIZE) / TRACE_RECORD_SIZE)

#define TRACE_KIND_MMIO             1
#define TRACE_KIND_SMC_ENTER        2
#define TRACE_KIND_SMC_REGS         3
#define TRACE_KIND_SMC_EXIT         4
#define TRACE_KIND_CMD              5
#define TRACE_KIND_GUEST            6

#define TRACE_MMIO_WRITE            (1u << 0)
#define TRACE_MMIO_FORWARDED        (1u << 1)
#define TRACE_MMIO_UNHANDLED        (1u << 2)

#define TRACE_SMC_REGS_EXIT         (1u << 0)
#define TRACE_SMC_FORWARDED         (1u << 1)
#define TRACE_SMC_UNHANDLED         (1u << 2)

#define TRACE_CMD_ACCEPTED          (1u << 0)
#define TRACE_CMD_REJECTED_VERB     (1u << 2)

#define TRACE_CMD_VERB_NONE         0
#define TRACE_CMD_VERB_PING         1
#define TRACE_CMD_VERB_TRACE_DUMP   2
#define TRACE_CMD_VERB_HELP         3
#define TRACE_CMD_VERB_GUEST_START  4
#define TRACE_CMD_VERB_GUEST_STOP   5
#define TRACE_CMD_VERB_STATUS       6
#define TRACE_CMD_VERB_GUEST_REGS   7
#define TRACE_CMD_VERB_GIC_DUMP     8

#define TRACE_GUEST_STARTED             1
#define TRACE_GUEST_STOPPED_BY_COMMAND  2
#define TRACE_GUEST_STOPPED_BY_FAULT    3

#define TRACE_PRODUCER_VMM          0
#define TRACE_PRODUCER_UARTRX       1

struct trace_record {
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

_Static_assert(sizeof(struct trace_record) == TRACE_RECORD_SIZE, "trace record is 64 bytes");

struct trace_header {
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

_Static_assert(sizeof(struct trace_header) == TRACE_HEADER_SIZE, "trace header is 336 bytes");

struct trace_ring {
    uint64_t head;
    uint64_t tail;
    uint64_t next_seq;
    uint32_t producer_lock;
    uint8_t reserved[TRACE_RING_HEADER_SIZE - 3 * sizeof(uint64_t) - sizeof(uint32_t)];
    struct trace_record records[TRACE_RING_CAPACITY];
};

_Static_assert(sizeof(struct trace_ring) <= TRACE_RING_SIZE, "trace ring fits its memory region");
