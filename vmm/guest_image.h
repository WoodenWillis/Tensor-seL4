/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <trace/cmd_ring.h>

struct guest_boot {
    uintptr_t pc;
    uintptr_t dtb;
    uintptr_t initrd;
};

bool guest_image_load(struct guest_boot *boot, uint64_t select);
const char *guest_image_name(uint64_t select);
