/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

#include <trace/cmd_ring.h>

void breadcrumb_init(uintptr_t cmd_ring_vaddr);
void breadcrumb_enter(enum vmm_phase phase, uint64_t label, uint64_t mr0, uint64_t mr1);
void breadcrumb_leave(void);
void breadcrumb_console_wait(void);
