/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>

bool smc_fault_hsr(microkit_msginfo msginfo, uint64_t *hsr);
bool smc_policy_handle(size_t vcpu_id, uint64_t hsr);
