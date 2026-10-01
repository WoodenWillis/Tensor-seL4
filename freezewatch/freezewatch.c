/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

#include <hw/exynos_uart.h>
#include <trace/console_ring.h>

#include "coresight.h"

#define STALL_SECONDS 3u

uintptr_t uart_vaddr;
uintptr_t vmm_console_vaddr;
uintptr_t uartrx_console_vaddr;

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

static uint32_t uart_read(uint32_t reg)
{
    return *(volatile uint32_t *)(uart_vaddr + reg);
}

static void uart_write(uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(uart_vaddr + reg) = val;
}

static void uart_tx_putc(char c)
{
    while (uart_read(EXYNOS_UART_UFSTAT) & EXYNOS_UART_UFSTAT_TXFULL) {
    }
    uart_write(EXYNOS_UART_UTXH, (uint32_t)(unsigned char)c);
}

static void uart_tx_puts(const char *s)
{
    for (; *s != '\0'; s++) {
        if (*s == '\n') {
            uart_tx_putc('\r');
        }
        uart_tx_putc(*s);
    }
}

static bool ring_pending(uintptr_t vaddr)
{
    const struct console_ring *ring = (const struct console_ring *)vaddr;

    return __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE) != __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
}

static bool console_stuck(void)
{
    bool pending = ring_pending(vmm_console_vaddr) || ring_pending(uartrx_console_vaddr);

    return pending && (uart_read(EXYNOS_UART_UTRSTAT) & EXYNOS_UART_UTRSTAT_TXE);
}

static void report_freeze(void)
{
    uart_tx_puts("\nFREEZEWATCH: console output pending and the UART idle for 3 s; sampling every core\n");
    cs_sample_all(uart_tx_puts);
    uart_tx_puts("FREEZEWATCH: done\n");
}

static void watch_forever(void)
{
    uint64_t stall_limit = read_cntfrq() * STALL_SECONDS;
    uint64_t stuck_since = 0;
    bool reported = false;

    for (;;) {
        uint64_t now = read_cntpct();
        if (!console_stuck()) {
            stuck_since = 0;
            reported = false;
        } else if (stuck_since == 0) {
            stuck_since = now;
        } else if (!reported && now - stuck_since >= stall_limit) {
            report_freeze();
            reported = true;
        }
    }
}

void init(void)
{
    microkit_dbg_puts("FREEZEWATCH|INFO: watching for a frozen console on CPU 5\n");
    watch_forever();
}

void notified(microkit_channel ch)
{
}
