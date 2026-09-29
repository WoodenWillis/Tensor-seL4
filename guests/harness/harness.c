/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdint.h>

#include <hw/exynos_uart.h>

#include "harness_map.h"

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

static void uart_puthex32(uint32_t val)
{
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc("0123456789abcdef"[(val >> shift) & 0xf]);
    }
}

void harness_main(void)
{
    uart_puts("hello\n");
    uart_puts("WTCON=");
    uart_puthex32(mmio_read32(HARNESS_WATCHDOG_GPA + HARNESS_WATCHDOG_WTCON));
    uart_puts("\n");
    (void)mmio_read32(HARNESS_UNKNOWN_DEVICE_GPA);
    uart_puts("resumed\n");
}
