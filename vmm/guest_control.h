/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void guest_control_start(void);
void guest_control_stop(void);
void guest_control_status(void);
void guest_control_regs(void);
enum guest_fault_kind {
    GUEST_FAULT_SMC,
    GUEST_FAULT_MEMORY,
    GUEST_FAULT_VCPU,
    GUEST_FAULT_OTHER,
};

struct guest_fault {
    enum guest_fault_kind kind;
    uint64_t detail;
};

void guest_control_fault_stopped(size_t vcpu_id, struct guest_fault fault);
