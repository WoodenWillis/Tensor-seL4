/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

#define CONSOLE_RING_SIZE      0x4000
#define CONSOLE_RING_CAPACITY  (CONSOLE_RING_SIZE - 2 * sizeof(uint64_t))

struct console_ring {
    uint64_t head;
    uint64_t tail;
    char buf[CONSOLE_RING_CAPACITY];
};

_Static_assert(sizeof(struct console_ring) == CONSOLE_RING_SIZE, "console ring fills its memory region");
