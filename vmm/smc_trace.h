/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <microkit.h>

void smc_trace_enter(size_t vcpu_id, uintptr_t pc, uint64_t hsr, const seL4_UserContext *regs);
void smc_trace_exit(size_t vcpu_id, uintptr_t pc, uint64_t hsr, uint8_t flags, const seL4_UserContext *regs);
void smc_trace_unhandled(size_t vcpu_id, uintptr_t pc, uint64_t hsr);
