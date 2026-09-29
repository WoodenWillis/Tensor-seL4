/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

#include <trace/trace.h>

#define TRACE_ARCHIVE_SIZE          0x800000
#define TRACE_ARCHIVE_HEADER_SIZE   64
#define TRACE_ARCHIVE_CAPACITY      ((TRACE_ARCHIVE_SIZE - TRACE_ARCHIVE_HEADER_SIZE) / TRACE_RECORD_SIZE)

struct trace_archive {
    uint64_t count;
    uint64_t not_archived;
    uint8_t reserved[TRACE_ARCHIVE_HEADER_SIZE - 2 * sizeof(uint64_t)];
    struct trace_record records[TRACE_ARCHIVE_CAPACITY];
};

_Static_assert(sizeof(struct trace_archive) <= TRACE_ARCHIVE_SIZE, "trace archive fits its memory region");

#define TRACE_CONTROL_SIZE          0x1000

struct trace_control {
    uint64_t dump_requests;
};
