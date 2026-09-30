/* SPDX-License-Identifier: BSD-2-Clause */

#include <microkit.h>
#include <libvmm/libvmm.h>

#include <trace/trace.h>

#include "guest_control.h"
#include "guest_image.h"
#include "guest_map.h"
#include "trace_producer.h"

enum guest_state {
    GUEST_NOT_STARTED,
    GUEST_RUNNING,
    GUEST_STOPPED_BY_COMMAND,
    GUEST_STOPPED_BY_FAULT,
    GUEST_START_FAILED,
};

struct fault_stop {
    uintptr_t pc;
    bool is_smc;
    uint64_t detail;
};

static enum guest_state state = GUEST_NOT_STARTED;
static uint64_t run;
static struct fault_stop last_fault;

static void trace_guest_event(uint64_t event, uintptr_t pc)
{
    struct trace_record rec = {
        .pc = pc,
        .addr = event,
        .value = run,
        .vcpu = GUEST_BOOT_VCPU_ID,
        .kind = TRACE_KIND_GUEST,
    };

    trace_emit(&rec);
}

static bool boot_fresh(void)
{
    struct guest_boot boot;

    if (!guest_image_load(&boot)) {
        state = GUEST_START_FAILED;
        return false;
    }
    /* TODO(will): libvmm has no vGIC reset; state from a previous run survives a restart */
    vcpu_reset(GUEST_BOOT_VCPU_ID);
    run++;
    trace_guest_event(TRACE_GUEST_STARTED, boot.pc);
    if (!guest_start(boot.pc, boot.dtb, boot.initrd)) {
        LOG_VMM_ERR("run %lu: failed to start the vCPU\n", run);
        state = GUEST_START_FAILED;
        return false;
    }
    state = GUEST_RUNNING;
    return true;
}

void guest_control_start(void)
{
    if (state == GUEST_RUNNING) {
        LOG_VMM("guest-start refused: run %lu is already running (use guest-stop first)\n", run);
        return;
    }
    if (boot_fresh()) {
        LOG_VMM("run %lu started from a fresh %s image\n", run, GUEST_NAME);
    }
}

void guest_control_stop(void)
{
    if (state != GUEST_RUNNING) {
        LOG_VMM("guest-stop refused: no guest is running. Current state:\n");
        guest_control_status();
        return;
    }
    microkit_vcpu_stop(GUEST_BOOT_VCPU_ID);
    vcpu_set_on(GUEST_BOOT_VCPU_ID, false);
    state = GUEST_STOPPED_BY_COMMAND;
    trace_guest_event(TRACE_GUEST_STOPPED_BY_COMMAND, 0);
    LOG_VMM("run %lu stopped by command\n", run);
}

static void print_fault_stop(void)
{
    if (last_fault.is_smc) {
        LOG_VMM("guest stopped by fault (run %lu): SMC 0x%lx refused by policy at pc 0x%lx\n", run,
                last_fault.detail, last_fault.pc);
    } else {
        LOG_VMM("guest stopped by fault (run %lu): unhandled access to 0x%lx at pc 0x%lx\n", run,
                last_fault.detail, last_fault.pc);
    }
}

void guest_control_status(void)
{
    switch (state) {
    case GUEST_NOT_STARTED:
        LOG_VMM("guest not started (type guest-start)\n");
        break;
    case GUEST_RUNNING:
        LOG_VMM("guest running (run %lu)\n", run);
        break;
    case GUEST_STOPPED_BY_COMMAND:
        LOG_VMM("guest stopped by command (run %lu)\n", run);
        break;
    case GUEST_STOPPED_BY_FAULT:
        print_fault_stop();
        break;
    case GUEST_START_FAILED:
        LOG_VMM("guest failed to start (run %lu); see the error above\n", run);
        break;
    }
}

void guest_control_fault_stopped(size_t vcpu_id, bool is_smc, uintptr_t fault_addr)
{
    seL4_UserContext regs = { 0 };
    seL4_Error err = seL4_TCB_ReadRegisters(BASE_VM_TCB_CAP + vcpu_id, false, 0, SEL4_USER_CONTEXT_SIZE, &regs);

    if (err != seL4_NoError) {
        LOG_VMM_ERR("seL4_TCB_ReadRegisters returned %d; fault pc unknown\n", err);
    }
    microkit_vcpu_stop(vcpu_id);
    vcpu_set_on(vcpu_id, false);
    last_fault = (struct fault_stop) {
        .pc = regs.pc,
        .is_smc = is_smc,
        .detail = is_smc ? regs.x0 : fault_addr,
    };
    state = GUEST_STOPPED_BY_FAULT;
    trace_guest_event(TRACE_GUEST_STOPPED_BY_FAULT, regs.pc);
    print_fault_stop();
}
