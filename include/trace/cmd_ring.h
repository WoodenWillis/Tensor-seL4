/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

#define CMD_RING_SIZE      0x1000
#define CMD_RING_CAPACITY  64

struct cmd_entry {
    uint64_t id;
    uint64_t verb;
};

enum vmm_phase {
    VMM_PHASE_IDLE,
    VMM_PHASE_FAULT,
    VMM_PHASE_NOTIFIED,
};

/* TODO(will): remove the breadcrumb once the VMM hang is understood */
struct vmm_breadcrumb {
    uint64_t seq;
    uint64_t phase;
    uint64_t since;
    uint64_t label;
    uint64_t mr0;
    uint64_t mr1;
    uint64_t console_waits;
};

struct cmd_ring {
    uint64_t head;
    uint64_t tail;
    uint64_t completed_id;
    uint64_t reserved;
    struct cmd_entry entries[CMD_RING_CAPACITY];
    struct vmm_breadcrumb breadcrumb;
};

_Static_assert(sizeof(struct cmd_ring) <= CMD_RING_SIZE, "command ring fits its memory region");
