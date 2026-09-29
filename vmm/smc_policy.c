/* SPDX-License-Identifier: BSD-2-Clause */

#include <libvmm/libvmm.h>
#include <libvmm/arch/aarch64/fault.h>
#include <libvmm/arch/aarch64/hsr.h>
#include <libvmm/arch/aarch64/psci.h>
#include <libvmm/arch/aarch64/smc.h>

#include <trace/trace.h>

#include "smc_policy.h"
#include "smc_trace.h"

/* SMC Calling Convention (DEN0028) */
#define SMCCC_SERVICE(fid)        (((fid) >> 24) & 0x3f)
#define SMCCC_FUNC_NUMBER(fid)    ((fid) & 0xffff)
#define SMCCC_SERVICE_STD         4
/* SMCCC_VERSION */
#define SMCCC_VERSION_FID         0x80000000u

enum smc_action {
    SMC_EMULATE_PSCI,
    SMC_FORWARD,
    SMC_UNHANDLED,
};

static enum smc_action smc_policy(uint32_t fid)
{
    if (SMCCC_SERVICE(fid) == SMCCC_SERVICE_STD && SMCCC_FUNC_NUMBER(fid) < PSCI_MAX) {
        return SMC_EMULATE_PSCI;
    }
    if (fid == SMCCC_VERSION_FID) {
        return SMC_FORWARD;
    }
    return SMC_UNHANDLED;
}

static bool read_regs(size_t vcpu_id, seL4_UserContext *regs)
{
    seL4_Error err = seL4_TCB_ReadRegisters(BASE_VM_TCB_CAP + vcpu_id, false, 0, SEL4_USER_CONTEXT_SIZE, regs);

    if (err != seL4_NoError) {
        LOG_VMM_ERR("seL4_TCB_ReadRegisters returned %d\n", err);
        return false;
    }
    return true;
}

static void smc_forward(seL4_UserContext *regs)
{
    seL4_ARM_SMCContext request = {
        .x0 = regs->x0, .x1 = regs->x1, .x2 = regs->x2, .x3 = regs->x3,
        .x4 = regs->x4, .x5 = regs->x5, .x6 = regs->x6, .x7 = regs->x7,
    };
    seL4_ARM_SMCContext response;

    microkit_arm_smc_call(&request, &response);
    regs->x0 = response.x0;
    regs->x1 = response.x1;
    regs->x2 = response.x2;
    regs->x3 = response.x3;
    regs->x4 = response.x4;
    regs->x5 = response.x5;
    regs->x6 = response.x6;
    regs->x7 = response.x7;
}

static bool smc_emulate_psci(size_t vcpu_id, uintptr_t pc, uint64_t hsr)
{
    seL4_UserContext regs;

    if (!smc_handle(vcpu_id, hsr) || !read_regs(vcpu_id, &regs)) {
        return false;
    }
    smc_trace_exit(vcpu_id, pc, hsr, 0, &regs);
    return true;
}

static bool smc_forward_and_resume(size_t vcpu_id, uintptr_t pc, uint64_t hsr, seL4_UserContext *regs)
{
    smc_forward(regs);
    smc_trace_exit(vcpu_id, pc, hsr, TRACE_SMC_FORWARDED, regs);
    return fault_advance_vcpu(vcpu_id, regs, SEL4_USER_CONTEXT_SIZE);
}

bool smc_fault_hsr(microkit_msginfo msginfo, uint64_t *hsr)
{
    if (microkit_msginfo_get_label(msginfo) != seL4_Fault_VCPUFault) {
        return false;
    }
    *hsr = microkit_mr_get(seL4_VCPUFault_HSR);
    return HSR_EXCEPTION_CLASS(*hsr) == HSR_SMC_64_EXCEPTION;
}

bool smc_policy_handle(size_t vcpu_id, uint64_t hsr)
{
    seL4_UserContext regs;

    if (!read_regs(vcpu_id, &regs)) {
        return false;
    }
    uintptr_t pc = regs.pc;
    smc_trace_enter(vcpu_id, pc, hsr, &regs);

    bool handled = false;
    switch (smc_policy((uint32_t)regs.x0)) {
    case SMC_EMULATE_PSCI:
        handled = smc_emulate_psci(vcpu_id, pc, hsr);
        break;
    case SMC_FORWARD:
        handled = smc_forward_and_resume(vcpu_id, pc, hsr, &regs);
        break;
    case SMC_UNHANDLED:
        break;
    }
    if (!handled) {
        smc_trace_unhandled(vcpu_id, pc, hsr);
        LOG_VMM_ERR("SMC 0x%lx at pc 0x%lx not handled by policy\n", regs.x0, pc);
    }
    return handled;
}
