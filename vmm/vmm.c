/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <libvmm/libvmm.h>
#include <sddf/util/cache.h>

#include "harness_map.h"
#include "exynos_uart_emul.h"
#include "mmio_trace.h"

extern char _guest_harness_image[];
extern char _guest_harness_image_end[];

uintptr_t guest_ram_vaddr;

static bool harness_load(void)
{
    size_t size = _guest_harness_image_end - _guest_harness_image;

    if (size == 0 || size > HARNESS_RAM_SIZE) {
        LOG_VMM_ERR("harness image size 0x%lx does not fit guest RAM 0x%x\n", size, HARNESS_RAM_SIZE);
        return false;
    }
    memcpy((void *)guest_ram_vaddr, _guest_harness_image, size);
    cache_clean_and_invalidate(guest_ram_vaddr, guest_ram_vaddr + size);
    return true;
}

void init(void)
{
    LOG_VMM("starting \"%s\"\n", microkit_name);

    arch_guest_init_t args = {
        .num_vcpus = 1,
        .num_guest_ram_regions = 1,
        .guest_ram_regions = { (struct guest_ram_region) {
            .gpa_start = HARNESS_RAM_GPA, .size = HARNESS_RAM_SIZE, .vmm_vaddr = (void *)guest_ram_vaddr } },
    };

    if (!guest_init(args)) {
        LOG_VMM_ERR("failed to initialise guest\n");
        return;
    }
    if (!harness_load()) {
        return;
    }
    if (!exynos_uart_emul_init(HARNESS_UART_GPA, HARNESS_UART_SIZE, "harness")) {
        LOG_VMM_ERR("failed to register UART emulation\n");
        return;
    }
    if (!guest_start(HARNESS_RAM_GPA, 0, 0)) {
        LOG_VMM_ERR("failed to start guest\n");
    }
}

void notified(microkit_channel ch)
{
    LOG_VMM_ERR("unexpected notification on channel %u\n", ch);
}

struct vm_fault {
    bool is_vm_fault;
    uintptr_t pc;
    uintptr_t addr;
    size_t fsr;
};

static struct vm_fault vm_fault_save(microkit_msginfo msginfo)
{
    struct vm_fault f = { .is_vm_fault = microkit_msginfo_get_label(msginfo) == seL4_Fault_VMFault };

    if (f.is_vm_fault) {
        f.pc = microkit_mr_get(seL4_VMFault_IP);
        f.addr = microkit_mr_get(seL4_VMFault_Addr);
        f.fsr = microkit_mr_get(seL4_VMFault_FSR);
    }
    return f;
}

static void guest_stop_unhandled(microkit_child child, const struct vm_fault *f)
{
    if (f->is_vm_fault) {
        mmio_trace_unhandled(child, f->pc, f->addr, f->fsr);
    }
    microkit_vcpu_stop(child);
    LOG_VMM_ERR("guest stopped: fault not handled\n");
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    struct vm_fault f = vm_fault_save(msginfo);

    if (!fault_handle(child, msginfo)) {
        guest_stop_unhandled(child, &f);
        return seL4_False;
    }
    *reply_msginfo = microkit_msginfo_new(0, 0);
    return seL4_True;
}
