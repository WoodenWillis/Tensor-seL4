/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

#define CMD_RING_SIZE      0x1000
#define CMD_RING_CAPACITY  64

struct cmd_entry {
    uint64_t id;
    uint64_t verb;
};

struct cmd_ring {
    uint64_t head;
    uint64_t tail;
    uint64_t completed_id;
    uint64_t reserved;
    struct cmd_entry entries[CMD_RING_CAPACITY];
};

_Static_assert(sizeof(struct cmd_ring) <= CMD_RING_SIZE, "command ring fits its memory region");
