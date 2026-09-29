/* SPDX-License-Identifier: BSD-2-Clause */

#include "smc_trace.h"
#include "trace_producer.h"

static void smc_trace_emit(uint8_t kind, size_t vcpu_id, uintptr_t pc, uint64_t hsr, uint8_t flags,
                           uint8_t first_reg, uint64_t a, uint64_t b)
{
    struct trace_record rec = {
        .pc = pc,
        .addr = a,
        .value = b,
        .esr = (uint32_t)hsr,
        .vcpu = (uint16_t)vcpu_id,
        .kind = kind,
        .size = first_reg,
        .flags = flags,
    };

    trace_emit(&rec);
}

static void smc_trace_regs(size_t vcpu_id, uintptr_t pc, uint64_t hsr, uint8_t flags,
                           const seL4_UserContext *r)
{
    smc_trace_emit(TRACE_KIND_SMC_REGS, vcpu_id, pc, hsr, flags, 2, r->x2, r->x3);
    smc_trace_emit(TRACE_KIND_SMC_REGS, vcpu_id, pc, hsr, flags, 4, r->x4, r->x5);
    smc_trace_emit(TRACE_KIND_SMC_REGS, vcpu_id, pc, hsr, flags, 6, r->x6, r->x7);
}

void smc_trace_enter(size_t vcpu_id, uintptr_t pc, uint64_t hsr, const seL4_UserContext *regs)
{
    smc_trace_emit(TRACE_KIND_SMC_ENTER, vcpu_id, pc, hsr, 0, 0, regs->x0, regs->x1);
    smc_trace_regs(vcpu_id, pc, hsr, 0, regs);
}

void smc_trace_exit(size_t vcpu_id, uintptr_t pc, uint64_t hsr, uint8_t flags, const seL4_UserContext *regs)
{
    smc_trace_emit(TRACE_KIND_SMC_EXIT, vcpu_id, pc, hsr, flags, 0, regs->x0, regs->x1);
    smc_trace_regs(vcpu_id, pc, hsr, TRACE_SMC_REGS_EXIT, regs);
}

void smc_trace_unhandled(size_t vcpu_id, uintptr_t pc, uint64_t hsr)
{
    smc_trace_emit(TRACE_KIND_SMC_EXIT, vcpu_id, pc, hsr, TRACE_SMC_UNHANDLED, 0, 0, 0);
}
