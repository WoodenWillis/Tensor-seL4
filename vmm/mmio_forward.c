/* SPDX-License-Identifier: BSD-2-Clause */

#include <libvmm/libvmm.h>

#include "mmio_forward.h"
#include "mmio_trace.h"

#include <trace/trace.h>

static bool forward_access_supported(const struct mmio_forward *fwd, size_t offset, size_t fsr)
{
    if (fault_is_write(fsr)) {
        LOG_VMM_ERR("%s: write at offset 0x%lx refused, forwarding is read-only\n", fwd->name, offset);
        return false;
    }
    if (fault_get_width_bytes(fsr) != sizeof(uint32_t) || (offset & (sizeof(uint32_t) - 1))) {
        LOG_VMM_ERR("%s: %d-byte access at offset 0x%lx not forwarded\n", fwd->name,
                    fault_get_width_bytes(fsr), offset);
        return false;
    }
    return true;
}

static bool forward_fault(size_t vcpu_id, size_t offset, size_t fsr, seL4_UserContext *regs, void *data)
{
    struct mmio_forward *fwd = data;

    if (!forward_access_supported(fwd, offset, fsr)) {
        return false;
    }

    uint32_t val = *(volatile uint32_t *)(fwd->vmm_vaddr + offset);
    fault_emulate_write(regs, offset, fsr, val);
    mmio_trace(vcpu_id, regs, fwd->gpa + offset, fsr, TRACE_MMIO_FORWARDED, val);
    return true;
}

bool mmio_forward_read_only_init(struct mmio_forward *fwd)
{
    if (fwd->vmm_vaddr == 0 || fwd->size == 0) {
        LOG_VMM_ERR("%s: no VMM mapping for forwarded region\n", fwd->name);
        return false;
    }
    return fault_register_vm_exception_handler(fwd->gpa, fwd->size, forward_fault, fwd);
}
