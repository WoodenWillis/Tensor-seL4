/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdint.h>

#include <hw/exynos_uart.h>

#include "harness_map.h"
#include "harness_uart.h"

static uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static void mmio_write32(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

void uart_putc(char c)
{
    while (!(mmio_read32(HARNESS_UART_GPA + EXYNOS_UART_UTRSTAT) & EXYNOS_UART_UTRSTAT_TXFE)) {
    }
    mmio_write32(HARNESS_UART_GPA + EXYNOS_UART_UTXH, (uint8_t)c);
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

static void uart_puthex(uint64_t val, int digits)
{
    uart_puts("0x");
    for (int shift = 4 * (digits - 1); shift >= 0; shift -= 4) {
        uart_putc("0123456789abcdef"[(val >> shift) & 0xf]);
    }
}

void uart_puthex32(uint32_t val)
{
    uart_puthex(val, 8);
}

void uart_puthex64(uint64_t val)
{
    uart_puthex(val, 16);
}

void uart_putdec(uint64_t val)
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
