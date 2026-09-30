/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>
#include <microkit.h>

void guest_stats_reset(void);
void guest_stats_count(seL4_Word label, uint64_t hsr);
void guest_stats_print(void);
