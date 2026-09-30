/* SPDX-License-Identifier: BSD-2-Clause */

#include <microkit.h>
#include <libvmm/libvmm.h>
#include <libvmm/arch/aarch64/hsr.h>

#include "guest_stats.h"

enum guest_exit {
    GUEST_EXIT_MEMORY,
    GUEST_EXIT_SMC,
    GUEST_EXIT_WFX,
    GUEST_EXIT_SYSREG,
    GUEST_EXIT_VPPI,
    GUEST_EXIT_VGIC_MAINTENANCE,
    GUEST_EXIT_OTHER,
    GUEST_EXIT_COUNT,
};

static const char *const exit_names[GUEST_EXIT_COUNT] = {
    [GUEST_EXIT_MEMORY] = "memory access",
    [GUEST_EXIT_SMC] = "SMC",
    [GUEST_EXIT_WFX] = "WFI/WFE",
    [GUEST_EXIT_SYSREG] = "sysreg",
    [GUEST_EXIT_VPPI] = "vtimer inject",
    [GUEST_EXIT_VGIC_MAINTENANCE] = "vGIC maint",
    [GUEST_EXIT_OTHER] = "other",
};

struct exit_stat {
    uint64_t count;
    uint64_t last;
};

static struct exit_stat stats[GUEST_EXIT_COUNT];

static uint64_t read_cntpct(void)
{
    uint64_t val;
    asm volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static uint64_t read_cntfrq(void)
{
    uint64_t val;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static enum guest_exit vcpu_exit_of(uint64_t hsr)
{
    switch (HSR_EXCEPTION_CLASS(hsr)) {
    case HSR_SMC_64_EXCEPTION:
        return GUEST_EXIT_SMC;
    case HSR_WFx_EXCEPTION:
        return GUEST_EXIT_WFX;
    case HSR_SYSREG_64_EXCEPTION:
        return GUEST_EXIT_SYSREG;
    default:
        return GUEST_EXIT_OTHER;
    }
}

static enum guest_exit exit_of(seL4_Word label, uint64_t hsr)
{
    switch (label) {
    case seL4_Fault_VMFault:
        return GUEST_EXIT_MEMORY;
    case seL4_Fault_VCPUFault:
        return vcpu_exit_of(hsr);
    case seL4_Fault_VPPIEvent:
        return GUEST_EXIT_VPPI;
    case seL4_Fault_VGICMaintenance:
        return GUEST_EXIT_VGIC_MAINTENANCE;
    default:
        return GUEST_EXIT_OTHER;
    }
}

void guest_stats_reset(void)
{
    for (int i = 0; i < GUEST_EXIT_COUNT; i++) {
        stats[i] = (struct exit_stat) { 0 };
    }
}

void guest_stats_count(seL4_Word label, uint64_t hsr)
{
    struct exit_stat *s = &stats[exit_of(label, hsr)];

    s->count++;
    s->last = read_cntpct();
}

static void print_stat(const char *name, const struct exit_stat *s, uint64_t now, uint64_t freq)
{
    if (s->count == 0) {
        LOG_VMM("  %-14s %10lu\n", name, s->count);
        return;
    }
    uint64_t ms = (now - s->last) * 1000 / freq;
    LOG_VMM("  %-14s %10lu   last %lu.%03lu s ago\n", name, s->count, ms / 1000, ms % 1000);
}

void guest_stats_print(void)
{
    uint64_t now = read_cntpct();
    uint64_t freq = read_cntfrq();

    LOG_VMM("guest exits this run:\n");
    for (int i = 0; i < GUEST_EXIT_COUNT; i++) {
        print_stat(exit_names[i], &stats[i], now, freq);
    }
}
