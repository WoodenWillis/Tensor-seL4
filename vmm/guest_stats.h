/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>
#include <microkit.h>

void guest_stats_reset(void);
void guest_stats_count(seL4_Word label, uint64_t hsr);
void guest_stats_print(void);
void guest_stats_heartbeat(uint64_t notifications, uint64_t cmd_head, uint64_t cmd_tail);
