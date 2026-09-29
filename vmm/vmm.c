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
#include "mmio_forward.h"
#include "mmio_trace.h"
#include "smc_policy.h"
#include "trace_producer.h"
#include "channels.h"

extern char _guest_harness_image[];
extern char _guest_harness_image_end[];

#define TRACE_PRODUCER_ID 0

uintptr_t guest_ram_vaddr;
uintptr_t watchdog_cl0_vaddr;
uintptr_t trace_ring_vaddr;

static struct mmio_forward watchdog_cl0 = {
    .name = "watchdog_cl0",
    .gpa = HARNESS_WATCHDOG_GPA,
    .size = HARNESS_WATCHDOG_SIZE,
};

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
    trace_producer_init(trace_ring_vaddr, CH_TRACER, TRACE_PRODUCER_ID);

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
    if (!exynos_uart_emul_init(HARNESS_UART_GPA, HARNESS_UART_SIZE)) {
        LOG_VMM_ERR("failed to register UART emulation\n");
        return;
    }
    watchdog_cl0.vmm_vaddr = watchdog_cl0_vaddr;
    if (!mmio_forward_read_only_init(&watchdog_cl0)) {
        LOG_VMM_ERR("failed to register watchdog_cl0 forwarding\n");
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

static bool guest_fault_handle(microkit_child child, microkit_msginfo msginfo)
{
    struct vm_fault f = vm_fault_save(msginfo);

    if (fault_handle(child, msginfo)) {
        return true;
    }
    if (f.is_vm_fault) {
        mmio_trace_unhandled(child, f.pc, f.addr, f.fsr);
    }
    return false;
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    uint64_t hsr;
    bool handled;

    if (smc_fault_hsr(msginfo, &hsr)) {
        handled = smc_policy_handle(child, hsr);
    } else {
        handled = guest_fault_handle(child, msginfo);
    }
    if (!handled) {
        microkit_vcpu_stop(child);
        LOG_VMM_ERR("guest stopped: fault not handled\n");
        return seL4_False;
    }
    *reply_msginfo = microkit_msginfo_new(0, 0);
    return seL4_True;
}
