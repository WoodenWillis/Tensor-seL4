/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

#define CMD_RING_SIZE      0x1000
#define CMD_RING_CAPACITY  64

#define GUEST_SELECT_DEFAULT        0x0ull
#define GUEST_SELECT_LINUX          0x1ull
#define GUEST_SELECT_HARNESS        0x100ull
#define GUEST_SELECT_HARNESS_MODE(s) ((s) & 0xffull)
#define GUEST_SELECT_VCPU_SHIFT     16
#define GUEST_SELECT_VCPU(s)        (((s) >> GUEST_SELECT_VCPU_SHIFT) & 0xffull)
#define GUEST_SELECT_GUEST(s)       ((s) & 0xffffull)
#define GUEST_SELECT_IS_HARNESS(s)  ((GUEST_SELECT_GUEST(s) & ~0xffull) == GUEST_SELECT_HARNESS)

struct cmd_entry {
    uint64_t id;
    uint64_t verb;
    uint64_t arg;
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
