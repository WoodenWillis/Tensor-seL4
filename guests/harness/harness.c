/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdint.h>

#include <hw/exynos_uart.h>

#include "harness_map.h"

/* PSCI_VERSION */
#define PSCI_VERSION_FID            0x84000000u
/* SMCCC_VERSION */
#define SMCCC_VERSION_FID           0x80000000u
/* SiP service, function 0 */
#define HARNESS_UNHANDLED_SMC_FID   0x82000000u

void harness_main(void);

static uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static void mmio_write32(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static void uart_putc(char c)
{
    while (!(mmio_read32(HARNESS_UART_GPA + EXYNOS_UART_UTRSTAT) & EXYNOS_UART_UTRSTAT_TXFE)) {
    }
    mmio_write32(HARNESS_UART_GPA + EXYNOS_UART_UTXH, (uint8_t)c);
}

static void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

static uint64_t smc0(uint64_t fid)
{
    register uint64_t x0 asm("x0") = fid;

    asm volatile("smc #0"
                 : "+r"(x0)
                 :
                 : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14",
                   "x15", "x16", "x17", "memory");
    return x0;
}

static void uart_puthex32(uint32_t val)
{
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc("0123456789abcdef"[(val >> shift) & 0xf]);
    }
}

#ifdef HARNESS_TLBI_STRESS
#define TLBI_STRESS_ROUND 1000000u

static void uart_putdec(uint64_t val)
{
    char buf[21];
    int pos = sizeof(buf) - 1;

    buf[pos] = '\0';
    do {
        buf[--pos] = (char)('0' + val % 10);
        val /= 10;
    } while (val != 0);
    uart_puts(&buf[pos]);
}

static void tlbi_round(void)
{
    for (uint32_t i = 0; i < TLBI_STRESS_ROUND; i++) {
        asm volatile("tlbi vaale1is, xzr\n\tdsb ish" ::: "memory");
    }
}

static void tlbi_stress_forever(void)
{
    uart_puts("tlbi-stress at EL1: tlbi vaale1is + dsb ish, forever\n");
    for (uint64_t round = 1;; round++) {
        tlbi_round();
        uart_puts("tlbi-stress at EL1: ");
        uart_putdec(round * TLBI_STRESS_ROUND);
        uart_puts("\n");
    }
}
#endif

void harness_main(void)
{
    uart_puts("hello\n");
    uart_puts("WTCON=");
    uart_puthex32(mmio_read32(HARNESS_WATCHDOG_GPA + HARNESS_WATCHDOG_WTCON));
    uart_puts("\n");
    uart_puts("PSCI_VERSION=");
    uart_puthex32((uint32_t)smc0(PSCI_VERSION_FID));
    uart_puts("\n");
    uart_puts("SMCCC_VERSION=");
    uart_puthex32((uint32_t)smc0(SMCCC_VERSION_FID));
    uart_puts("\n");
#ifdef HARNESS_TLBI_STRESS
    tlbi_stress_forever();
#endif
    (void)smc0(HARNESS_UNHANDLED_SMC_FID);
    uart_puts("resumed\n");
}
