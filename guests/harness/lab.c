/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stdint.h>

#include "harness_map.h"
#include "harness_uart.h"
#include "lab.h"

/* libvmm vGICv3 at the physical GIC's addresses */
#define GICD_BASE               0x10400000u
#define GICR_BASE               0x10440000u
#define GICR_FRAME_SIZE         0x20000u
#define GICR_SGI_OFFSET         0x10000u
#define GICD_CTLR               0x0000u
#define GICD_CTLR_ENABLE_G1NS   (1u << 1)
#define GICD_CTLR_ARE_NS        (1u << 4)
#define GICR_ISENABLER0         0x0100u
#define VTIMER_INTID            27u
#define INTID_SPURIOUS_MIN      1020u

#define TIMER_PERIOD_TICKS      2458u
#define TLBI_REPORT_EVERY       1000000u
#define WALK_REPORT_EVERY       100000u

/* VMSAv8-64 descriptors, 4 KiB granule */
#define DESC_BLOCK              0x1ull
#define DESC_TABLE              0x3ull
#define DESC_PAGE               0x3ull
#define DESC_ATTR(n)            ((uint64_t)(n) << 2)
#define DESC_AP_RO              (1ull << 7)
#define DESC_SH_INNER           (3ull << 8)
#define DESC_AF                 (1ull << 10)
#define DESC_DBM                (1ull << 51)
#define DESC_PXN                (1ull << 53)
#define DESC_UXN                (1ull << 54)

/* MAIR_EL1 attr0 Device-nGnRnE, attr1 Normal WB */
#define MAIR_VALUE              0xff00ull
#define ATTR_DEVICE             0u
#define ATTR_NORMAL             1u

/* TCR_EL1 */
#define TCR_T0SZ_39BIT          25ull
#define TCR_IRGN0_WBWA          (1ull << 8)
#define TCR_ORGN0_WBWA          (1ull << 10)
#define TCR_SH0_INNER           (3ull << 12)
#define TCR_EPD1                (1ull << 23)
#define TCR_IPS_40BIT           (2ull << 32)
#define TCR_HA                  (1ull << 39)
#define TCR_HD                  (1ull << 40)

/* SCTLR_EL1 */
#define SCTLR_M                 (1ull << 0)
#define SCTLR_C                 (1ull << 2)
#define SCTLR_I                 (1ull << 12)

#define PAGE_SIZE               4096u
#define ENTRIES                 512u

extern char lab_vectors[];

static uint64_t l1[ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2[ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l3[ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static volatile uint32_t test_page[PAGE_SIZE / 4] __attribute__((aligned(PAGE_SIZE)));

struct walk {
    bool hw_updates;
    bool broadcast;
};

static volatile uint64_t timer_irqs;
static volatile uint64_t spurious_irqs;

static void mmio_write32(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static void report(uint64_t mode, uint64_t done, const char *what)
{
    uart_puts("harness mode ");
    uart_putdec(mode);
    uart_puts(": ");
    uart_putdec(done);
    uart_puts(what);
    uart_puts(", timer irqs ");
    uart_putdec(timer_irqs);
    uart_puts(", spurious ");
    uart_putdec(spurious_irqs);
    uart_puts("\n");
}

void lab_irq(void);
void lab_irq(void)
{
    uint64_t iar;

    asm volatile("mrs %0, S3_0_C12_C12_0" : "=r"(iar));
    if ((iar & 0xffffffu) >= INTID_SPURIOUS_MIN) {
        spurious_irqs++;
        return;
    }
    if ((iar & 0xffffffu) == VTIMER_INTID) {
        timer_irqs++;
        asm volatile("msr cntv_tval_el0, %0" : : "r"((uint64_t)TIMER_PERIOD_TICKS));
    }
    asm volatile("msr S3_0_C12_C12_1, %0\n\tisb" : : "r"(iar));
}

void lab_bad_exception(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far);
void lab_bad_exception(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far)
{
    uart_puts("harness: unexpected exception, vector ");
    uart_putdec(vector);
    uart_puts(" ESR ");
    uart_puthex64(esr);
    uart_puts(" ELR ");
    uart_puthex64(elr);
    uart_puts(" FAR ");
    uart_puthex64(far);
    uart_puts("\n");
    for (;;) {
        asm volatile("wfe");
    }
}

static uintptr_t gicr_sgi_base(void)
{
    uint64_t mpidr;

    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return GICR_BASE + (uintptr_t)(mpidr & 0xff) * GICR_FRAME_SIZE + GICR_SGI_OFFSET;
}

static void gic_cpu_interface_on(void)
{
    asm volatile("msr S3_0_C12_C12_5, %0\n\tisb" : : "r"(7ull));
    asm volatile("msr S3_0_C4_C6_0, %0" : : "r"(0xffull));
    asm volatile("msr S3_0_C12_C12_3, %0" : : "r"(0ull));
    asm volatile("msr S3_0_C12_C12_7, %0\n\tisb" : : "r"(1ull));
}

static void timer_irqs_on(void)
{
    asm volatile("msr vbar_el1, %0\n\tisb" : : "r"(lab_vectors));
    mmio_write32(GICD_BASE + GICD_CTLR, GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1NS);
    mmio_write32(gicr_sgi_base() + GICR_ISENABLER0, 1u << VTIMER_INTID);
    gic_cpu_interface_on();
    asm volatile("msr cntv_tval_el0, %0" : : "r"((uint64_t)TIMER_PERIOD_TICKS));
    asm volatile("msr cntv_ctl_el0, %0\n\tisb" : : "r"(1ull));
    asm volatile("msr daifclr, #2");
    uart_puts("harness: virtual timer firing every 100 us\n");
}

static uint64_t test_desc(bool hw_updates)
{
    uint64_t desc = (uint64_t)(uintptr_t)test_page | DESC_ATTR(ATTR_NORMAL) | DESC_SH_INNER | DESC_PAGE;

    return desc | (hw_updates ? (DESC_DBM | DESC_AP_RO) : DESC_AF);
}

static void build_page_tables(bool hw_updates)
{
    uintptr_t base = HARNESS_RAM_GPA;

    l1[0] = DESC_ATTR(ATTR_DEVICE) | DESC_AF | DESC_PXN | DESC_UXN | DESC_BLOCK;
    l1[2] = (uint64_t)(uintptr_t)l2 | DESC_TABLE;
    l2[0] = (uint64_t)(uintptr_t)l3 | DESC_TABLE;
    for (uint32_t i = 0; i < ENTRIES; i++) {
        l3[i] = (base + (uint64_t)i * PAGE_SIZE) | DESC_ATTR(ATTR_NORMAL) | DESC_SH_INNER | DESC_AF | DESC_PAGE;
    }
    l3[((uintptr_t)test_page - base) / PAGE_SIZE] = test_desc(hw_updates);
}

static void put_reg(const char *name, uint64_t val)
{
    uart_puts("harness: ");
    uart_puts(name);
    uart_puts(" ");
    uart_puthex64(val);
    uart_puts("\n");
}

static void report_mmu_regs(void)
{
    uint64_t mmfr1, tcr, sctlr;

    asm volatile("mrs %0, id_aa64mmfr1_el1" : "=r"(mmfr1));
    asm volatile("mrs %0, tcr_el1" : "=r"(tcr));
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    put_reg("ID_AA64MMFR1_EL1", mmfr1);
    put_reg("TCR_EL1", tcr);
    put_reg("SCTLR_EL1", sctlr);
}

static void mmu_on(bool hw_updates)
{
    uint64_t tcr = TCR_T0SZ_39BIT | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | TCR_SH0_INNER | TCR_EPD1 | TCR_IPS_40BIT |
                   (hw_updates ? (TCR_HA | TCR_HD) : 0);
    uint64_t sctlr;

    build_page_tables(hw_updates);
    asm volatile("dsb ish");
    asm volatile("msr mair_el1, %0" : : "r"(MAIR_VALUE));
    asm volatile("msr tcr_el1, %0" : : "r"(tcr));
    asm volatile("msr ttbr0_el1, %0\n\tisb" : : "r"((uint64_t)(uintptr_t)l1));
    asm volatile("tlbi vmalle1\n\tdsb nsh\n\tisb" ::: "memory");
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= SCTLR_M | SCTLR_C | SCTLR_I;
    asm volatile("msr sctlr_el1, %0\n\tisb" : : "r"(sctlr));
    uart_puts("harness: MMU on, identity map, hardware AF and dirty updates ");
    uart_puts(hw_updates ? "on\n" : "off\n");
    report_mmu_regs();
}

static void tlbi_forever(uint64_t mode)
{
    for (uint64_t done = 0;;) {
        for (uint32_t i = 0; i < TLBI_REPORT_EVERY; i++) {
            asm volatile("tlbi vaale1is, xzr\n\tdsb ish" ::: "memory");
        }
        done += TLBI_REPORT_EVERY;
        report(mode, done, " broadcast tlbi");
    }
}

static void invalidate_page(uint64_t va_page, bool broadcast)
{
    if (broadcast) {
        asm volatile("dsb ishst\n\ttlbi vaale1is, %0\n\tdsb ish\n\tisb" : : "r"(va_page) : "memory");
    } else {
        asm volatile("dsb nshst\n\ttlbi vaale1, %0\n\tdsb nsh\n\tisb" : : "r"(va_page) : "memory");
    }
}

static void walk_round(const struct walk *walk, uint64_t *slot, uint64_t va_page)
{
    *(volatile uint64_t *)slot = test_desc(walk->hw_updates);
    invalidate_page(va_page, walk->broadcast);
    test_page[0] = test_page[0] + 1;
}

static void report_hw_update(const uint64_t *slot)
{
    uint64_t desc = *(volatile const uint64_t *)slot;

    put_reg("test page descriptor", desc);
    if ((desc & DESC_AF) && !(desc & DESC_AP_RO)) {
        uart_puts("harness: hardware set AF and cleared the read-only bit\n");
    } else {
        uart_puts("harness: FAIL: the descriptor was not updated by hardware\n");
    }
}

static void walk_forever(uint64_t mode, const struct walk *walk)
{
    uint64_t *slot = &l3[((uintptr_t)test_page - HARNESS_RAM_GPA) / PAGE_SIZE];
    uint64_t va_page = (uint64_t)(uintptr_t)test_page >> 12;

    uart_puts(walk->broadcast ? "harness: broadcast tlbi\n" : "harness: local tlbi\n");
    for (uint64_t done = 0;;) {
        for (uint32_t i = 0; i < WALK_REPORT_EVERY; i++) {
            walk_round(walk, slot, va_page);
        }
        if (done == 0 && walk->hw_updates) {
            report_hw_update(slot);
        }
        done += WALK_REPORT_EVERY;
        report(mode, done, " pte resets + tlbi + touch");
    }
}

static void walk_with(uint64_t mode, bool timer, bool hw_updates, bool broadcast)
{
    const struct walk walk = { .hw_updates = hw_updates, .broadcast = broadcast };

    mmu_on(hw_updates);
    if (timer) {
        timer_irqs_on();
    }
    walk_forever(mode, &walk);
}

void lab_run(uint64_t mode)
{
    uart_puts("harness mode ");
    uart_putdec(mode);
    uart_puts("\n");
    switch (mode) {
    case 1:
        tlbi_forever(mode);
        break;
    case 2:
        timer_irqs_on();
        tlbi_forever(mode);
        break;
    case 3:
        walk_with(mode, false, true, true);
        break;
    case 4:
        walk_with(mode, true, true, true);
        break;
    case 5:
        walk_with(mode, true, false, true);
        break;
    case 6:
        walk_with(mode, true, true, false);
        break;
    case 7:
        walk_with(mode, true, false, false);
        break;
    default:
        uart_puts("harness: unknown mode; modes are 1 to 7\n");
        break;
    }
}
