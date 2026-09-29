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

void harness_main(void)
{
    uart_puts("hello\n");
    (void)mmio_read32(HARNESS_UNKNOWN_DEVICE_GPA);
    uart_puts("resumed\n");
}
