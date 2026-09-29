/* SPDX-License-Identifier: BSD-2-Clause */

#include <libvmm/libvmm.h>

#include "mmio_trace.h"

static uint64_t mmio_trace_seq;

static uint64_t read_cntpct(void)
{
    uint64_t val;
    asm volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(val));
    return val;
}

void mmio_trace(size_t vcpu_id, const seL4_UserContext *regs, uintptr_t addr, size_t fsr, bool is_write,
                uint64_t value)
{
    printf("VMM|MMIO seq=%lu t=%lu vcpu=%lu pc=0x%lx addr=0x%lx size=%d %s val=0x%lx esr=0x%lx\n",
           mmio_trace_seq++, read_cntpct(), vcpu_id, regs->pc, addr, fault_get_width_bytes(fsr),
           is_write ? "W" : "R", value, fsr);
}

void mmio_trace_unhandled(size_t vcpu_id, uintptr_t pc, uintptr_t addr, size_t fsr)
{
    printf("VMM|MMIO seq=%lu t=%lu vcpu=%lu pc=0x%lx addr=0x%lx size=%d %s UNHANDLED esr=0x%lx\n",
           mmio_trace_seq++, read_cntpct(), vcpu_id, pc, addr, fault_get_width_bytes(fsr),
           fault_is_write(fsr) ? "W" : "R", fsr);
}
