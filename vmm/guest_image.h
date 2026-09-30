/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct guest_boot {
    uintptr_t pc;
    uintptr_t dtb;
    uintptr_t initrd;
};

bool guest_image_load(struct guest_boot *boot);
