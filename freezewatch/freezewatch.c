/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

#include <hw/exynos_uart.h>
#include <trace/console_ring.h>

#include "coresight.h"

#define TX_STALL_SECONDS 3u
#define RX_STALL_SECONDS 4u

struct stall {
    uint64_t since;
    uint64_t limit;
};

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

static bool tx_stuck(void)
{
    bool pending = ring_pending(vmm_console_vaddr) || ring_pending(uartrx_console_vaddr);

    return pending && (uart_read(EXYNOS_UART_UTRSTAT) & EXYNOS_UART_UTRSTAT_TXE);
}

static bool rx_stuck(void)
{
    return (uart_read(EXYNOS_UART_UFSTAT) & (EXYNOS_UART_UFSTAT_RXCOUNT | EXYNOS_UART_UFSTAT_RXFULL)) != 0;
}

static bool stalled(struct stall *s, bool stuck, uint64_t now)
{
    if (!stuck) {
        s->since = 0;
        return false;
    }
    if (s->since == 0) {
        s->since = now;
    }
    return now - s->since >= s->limit;
}

static void report_freeze(const char *why)
{
    uart_tx_puts("\nFREEZEWATCH: ");
    uart_tx_puts(why);
    uart_tx_puts("; sampling every core\n");
    cs_sample_all(uart_tx_puts);
    uart_tx_puts("FREEZEWATCH: done\n");
}

static void watch_forever(void)
{
    uint64_t freq = read_cntfrq();
    struct stall tx = { .limit = freq * TX_STALL_SECONDS };
    struct stall rx = { .limit = freq * RX_STALL_SECONDS };
    bool reported = false;

    for (;;) {
        uint64_t now = read_cntpct();
        bool tx_fired = stalled(&tx, tx_stuck(), now);
        bool rx_fired = stalled(&rx, rx_stuck(), now);

        if (tx.since == 0 && rx.since == 0) {
            reported = false;
        }
        if (!reported && (tx_fired || rx_fired)) {
            report_freeze(rx_fired ? "received characters unread for 4 s" : "console output pending and the UART idle for 3 s");
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
