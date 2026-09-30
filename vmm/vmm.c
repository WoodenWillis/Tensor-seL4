/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include <libvmm/libvmm.h>

#include <trace/cmd_ring.h>
#include <trace/trace.h>

#include "guest_map.h"
#include "exynos_uart_emul.h"
#include "guest_control.h"
#include "guest_stats.h"
#include "mmio_forward.h"
#include "mmio_trace.h"
#include "smc_policy.h"
#include "trace_producer.h"
#include "channels.h"

uintptr_t guest_ram_vaddr;
uintptr_t trace_ring_vaddr;
uintptr_t cmd_ring_vaddr;

#ifdef GUEST_WATCHDOG_GPA
uintptr_t watchdog_cl0_vaddr;

static struct mmio_forward watchdog_cl0 = {
    .name = "watchdog_cl0",
    .gpa = GUEST_WATCHDOG_GPA,
    .size = GUEST_WATCHDOG_SIZE,
};
#endif

void init(void)
{
    LOG_VMM("starting \"%s\"\n", microkit_name);
    trace_producer_init(trace_ring_vaddr, CH_TRACER, TRACE_PRODUCER_VMM);

    arch_guest_init_t args = {
        .num_vcpus = 1,
        .num_guest_ram_regions = 1,
        .guest_ram_regions = { (struct guest_ram_region) {
            .gpa_start = GUEST_RAM_GPA, .size = GUEST_RAM_SIZE, .vmm_vaddr = (void *)guest_ram_vaddr } },
    };

    if (!guest_init(args)) {
        LOG_VMM_ERR("failed to initialise guest\n");
        return;
    }
    if (!exynos_uart_emul_init(GUEST_UART_GPA, GUEST_UART_SIZE)) {
        LOG_VMM_ERR("failed to register UART emulation\n");
        return;
    }
#ifdef GUEST_WATCHDOG_GPA
    watchdog_cl0.vmm_vaddr = watchdog_cl0_vaddr;
    if (!mmio_forward_read_only_init(&watchdog_cl0)) {
        LOG_VMM_ERR("failed to register watchdog_cl0 forwarding\n");
        return;
    }
#endif
    LOG_VMM("guest not started; type guest-start\n");
}

static void run_command(const struct cmd_entry *cmd)
{
    switch (cmd->verb) {
    case TRACE_CMD_VERB_GUEST_START:
        guest_control_start();
        break;
    case TRACE_CMD_VERB_GUEST_STOP:
        guest_control_stop();
        break;
    case TRACE_CMD_VERB_STATUS:
        guest_control_status();
        break;
    default:
        LOG_VMM_ERR("command %lu: verb %lu is not a VMM command\n", cmd->id, cmd->verb);
        break;
    }
}

static void run_commands(void)
{
    struct cmd_ring *ring = (struct cmd_ring *)cmd_ring_vaddr;
    uint64_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);

    while (tail != __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE)) {
        const struct cmd_entry *cmd = &ring->entries[tail % CMD_RING_CAPACITY];
        run_command(cmd);
        __atomic_store_n(&ring->completed_id, cmd->id, __ATOMIC_RELEASE);
        tail++;
        __atomic_store_n(&ring->tail, tail, __ATOMIC_RELEASE);
    }
}

/* TODO(will): remove the heartbeat once the WFx-storm deafness is understood */
static uint64_t notifications;

static void heartbeat(void)
{
    struct cmd_ring *ring = (struct cmd_ring *)cmd_ring_vaddr;

    guest_stats_heartbeat(notifications, __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE),
                          __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE));
}

void notified(microkit_channel ch)
{
    notifications++;
    if (ch != CH_UARTRX) {
        LOG_VMM_ERR("unexpected notification on channel %u\n", ch);
        return;
    }
    run_commands();
}

struct vm_fault {
    seL4_Word label;
    uintptr_t pc;
    uintptr_t addr;
    size_t fsr;
    uint64_t hsr;
};

static struct vm_fault vm_fault_save(microkit_msginfo msginfo)
{
    struct vm_fault f = { .label = microkit_msginfo_get_label(msginfo) };

    switch (f.label) {
    case seL4_Fault_VMFault:
        f.pc = microkit_mr_get(seL4_VMFault_IP);
        f.addr = microkit_mr_get(seL4_VMFault_Addr);
        f.fsr = microkit_mr_get(seL4_VMFault_FSR);
        break;
    case seL4_Fault_VCPUFault:
        f.hsr = microkit_mr_get(seL4_VCPUFault_HSR);
        break;
    }
    return f;
}

static struct guest_fault guest_fault_of(const struct vm_fault *f)
{
    switch (f->label) {
    case seL4_Fault_VMFault:
        return (struct guest_fault) { .kind = GUEST_FAULT_MEMORY, .detail = f->addr };
    case seL4_Fault_VCPUFault:
        return (struct guest_fault) { .kind = GUEST_FAULT_VCPU, .detail = f->hsr };
    default:
        return (struct guest_fault) { .kind = GUEST_FAULT_OTHER, .detail = f->label };
    }
}

static bool guest_fault_handle(microkit_child child, microkit_msginfo msginfo, struct guest_fault *stop)
{
    struct vm_fault f = vm_fault_save(msginfo);

    *stop = guest_fault_of(&f);
    if (fault_handle(child, msginfo)) {
        return true;
    }
    if (f.label == seL4_Fault_VMFault) {
        mmio_trace_unhandled(child, f.pc, f.addr, f.fsr);
    }
    return false;
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    uint64_t hsr = 0;
    struct guest_fault stop = { .kind = GUEST_FAULT_SMC };
    bool is_smc = smc_fault_hsr(msginfo, &hsr);
    bool handled;

    guest_stats_count(microkit_msginfo_get_label(msginfo), hsr);
    heartbeat();

    if (is_smc) {
        handled = smc_policy_handle(child, hsr);
    } else {
        handled = guest_fault_handle(child, msginfo, &stop);
    }
    if (!handled) {
        guest_control_fault_stopped(child, stop);
        return seL4_False;
    }
    *reply_msginfo = microkit_msginfo_new(0, 0);
    return seL4_True;
}
