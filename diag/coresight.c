/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stdint.h>

#include <hw/zumapro_coresight.h>

#include "coresight.h"

static uint32_t cs_read(uintptr_t base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static void cs_write(uintptr_t base, uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(base + offset) = val;
}

static uint64_t cs_read64(uintptr_t base, uint32_t offset)
{
    return *(volatile uint64_t *)(base + offset);
}

static void put_hex(cs_puts_fn puts, uint64_t val, int digits)
{
    static const char hex[] = "0123456789abcdef";
    char buf[19] = "0x";

    for (int i = 0; i < digits; i++) {
        buf[2 + i] = hex[(val >> (4 * (digits - 1 - i))) & 0xf];
    }
    buf[2 + digits] = '\0';
    puts(buf);
}

static void put_digit(cs_puts_fn puts, uint32_t val)
{
    char buf[2] = { (char)('0' + val % 10), '\0' };
    puts(buf);
}

static void sample_line(cs_puts_fn puts, uint32_t core, uint64_t pcsr)
{
    puts("  core ");
    put_digit(puts, core);
    puts(" PMUPCSR ");
    put_hex(puts, pcsr, 16);
    puts(" ns ");
    put_digit(puts, CS_PCSR_NS(pcsr));
    puts(" el ");
    put_digit(puts, CS_PCSR_EL(pcsr));
    puts("\n");
}

static bool sampleable(cs_puts_fn puts, uint32_t core, uintptr_t dbg)
{
    uint32_t prsr = cs_read(dbg, CS_DBGPRSR);

    puts("core ");
    put_digit(puts, core);
    puts(": DBGPRSR ");
    put_hex(puts, prsr, 8);
    puts(" MIDR ");
    put_hex(puts, cs_read(dbg, CS_MIDR), 8);
    puts("\n");
    if (!(prsr & CS_PRSR_POWER_UP) || (prsr & CS_PRSR_RESET_STATE)) {
        puts("  powered down or in reset; not sampled\n");
        return false;
    }
    return true;
}

static void sample_core(cs_puts_fn puts, uint32_t core)
{
    uintptr_t dbg = CS_DBG_VADDR(core);
    uintptr_t pmu = CS_PMU_VADDR(core);

    if (!sampleable(puts, core, dbg)) {
        return;
    }
    cs_write(dbg, CS_DBGLAR, CS_OSLOCK_MAGIC);
    cs_write(dbg, CS_DBGOSLAR, 0);
    cs_write(pmu, CS_DBGLAR, CS_OSLOCK_MAGIC);
    for (uint32_t i = 0; i < CS_PCSR_SAMPLES; i++) {
        (void)cs_read64(pmu, CS_PMUPCSR);
        sample_line(puts, core, cs_read64(pmu, CS_PMUPCSR));
    }
    cs_write(pmu, CS_DBGLAR, CS_LOCK);
}

void cs_sample_all(cs_puts_fn puts)
{
    for (uint32_t core = 0; core < CS_NUM_CORES; core++) {
        sample_core(puts, core);
    }
}
