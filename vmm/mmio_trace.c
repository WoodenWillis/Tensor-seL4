/* SPDX-License-Identifier: BSD-2-Clause */

#include <libvmm/libvmm.h>

#include "mmio_trace.h"
#include "trace_producer.h"

static void mmio_trace_emit(size_t vcpu_id, uintptr_t pc, uintptr_t addr, size_t fsr, uint8_t flags,
                            uint64_t value)
{
    struct trace_record_v0 rec = {
        .pc = pc,
        .addr = addr,
        .value = value,
        .esr = (uint32_t)fsr,
        .vcpu = (uint16_t)vcpu_id,
        .kind = TRACE_KIND_MMIO,
        .size = (uint8_t)fault_get_width_bytes(fsr),
        .flags = flags | (fault_is_write(fsr) ? TRACE_MMIO_WRITE : 0),
    };

    trace_emit(&rec);
}

void mmio_trace(size_t vcpu_id, const seL4_UserContext *regs, uintptr_t addr, size_t fsr, uint8_t flags,
                uint64_t value)
{
    mmio_trace_emit(vcpu_id, regs->pc, addr, fsr, flags, value);
}

void mmio_trace_unhandled(size_t vcpu_id, uintptr_t pc, uintptr_t addr, size_t fsr)
{
    mmio_trace_emit(vcpu_id, pc, addr, fsr, TRACE_MMIO_UNHANDLED, 0);
}
