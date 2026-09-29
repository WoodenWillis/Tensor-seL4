/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>
#include <microkit.h>

#include <trace/trace_v0.h>

void trace_producer_init(uintptr_t ring_vaddr, microkit_channel tracer_ch, uint8_t producer_id);
void trace_emit(struct trace_record_v0 *rec);
