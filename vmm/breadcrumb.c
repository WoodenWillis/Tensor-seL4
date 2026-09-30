/* SPDX-License-Identifier: BSD-2-Clause */

#include "breadcrumb.h"

static struct vmm_breadcrumb *crumb;

static uint64_t read_cntpct(void)
{
    uint64_t val;
    asm volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(val));
    return val;
}

void breadcrumb_init(uintptr_t cmd_ring_vaddr)
{
    crumb = &((struct cmd_ring *)cmd_ring_vaddr)->breadcrumb;
}

static void set_phase(enum vmm_phase phase)
{
    __atomic_store_n(&crumb->since, read_cntpct(), __ATOMIC_RELAXED);
    __atomic_store_n(&crumb->phase, phase, __ATOMIC_RELAXED);
    __atomic_fetch_add(&crumb->seq, 1, __ATOMIC_RELEASE);
}

void breadcrumb_enter(enum vmm_phase phase, uint64_t label, uint64_t mr0, uint64_t mr1)
{
    __atomic_store_n(&crumb->label, label, __ATOMIC_RELAXED);
    __atomic_store_n(&crumb->mr0, mr0, __ATOMIC_RELAXED);
    __atomic_store_n(&crumb->mr1, mr1, __ATOMIC_RELAXED);
    set_phase(phase);
}

void breadcrumb_leave(void)
{
    set_phase(VMM_PHASE_IDLE);
}

void breadcrumb_console_wait(void)
{
    __atomic_fetch_add(&crumb->console_waits, 1, __ATOMIC_RELAXED);
}
