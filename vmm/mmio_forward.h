/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mmio_forward {
    const char *name;
    uintptr_t gpa;
    uintptr_t vmm_vaddr;
    size_t size;
};

bool mmio_forward_read_only_init(struct mmio_forward *fwd);
