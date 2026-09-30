/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void guest_control_start(void);
void guest_control_stop(void);
void guest_control_status(void);
void guest_control_fault_stopped(size_t vcpu_id, bool is_smc, uintptr_t fault_addr);
