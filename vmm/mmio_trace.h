/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <microkit.h>

void mmio_trace(size_t vcpu_id, const seL4_UserContext *regs, uintptr_t addr, size_t fsr, uint8_t flags,
                uint64_t value);
void mmio_trace_unhandled(size_t vcpu_id, uintptr_t pc, uintptr_t addr, size_t fsr);
