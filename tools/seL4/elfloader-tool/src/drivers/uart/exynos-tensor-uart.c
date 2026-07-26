/*
 * Copyright (C) 2026 WoodenWillis
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * ELFLOADER-side console driver for the Tensor G4 (zumapro) debug UART.
 *
 * This is SEPARATE from the kernel driver (kernel/src/drivers/serial/
 * exynos-tensor-uart.c). The elfloader is its own binary: it runs with the
 * MMU off, has no UART_PPTR, and its console entry point is the WEAK symbol
 * plat_console_putchar() in src/drivers/uart/common.c. Defining a strong
 * plat_console_putchar() here overrides that no-op stub, which is why the
 * "ELF-loader started on ..." banner was never appearing -- printf() was
 * feeding the weak do-nothing default.
 *
 * MMU is off at this point, so we use the RAW PHYSICAL address 0x10870000
 * directly (not a kernel PPTR).
 *
 * Registers: <linux/serial_s3c.h>, all verified against the live tree.
 */

#include <printf.h>

#define UART_PADDR   0x10870000UL

#define UCON         0x04       /* control */
#define UFCON        0x08       /* FIFO control */
#define UTRSTAT      0x10       /* TX/RX status */
#define UFSTAT       0x18       /* FIFO status */
#define UTXH         0x20       /* TX holding */

#define UCON_MODE_MASK   0xFu   /* TX/RX mode bits; exynos_port_configured() */
#define UCON_TXRX_POLL   0x5u   /* TX+RX in polling/interrupt mode */
#define UFCON_FIFOMODE   (1u << 0)

/* S5PV210 layout -- NOT the legacy S3C2410 (1<<9). This is the one Exynos uses. */
#define UFSTAT_TXFULL    (1u << 24)
#define UTRSTAT_TXE      (1u << 2)

#define TX_SPIN_LIMIT    1000000u

#define REG(x)  (*(volatile unsigned int *)(UART_PADDR + (x)))

/*
 * Strong override of the WEAK plat_console_putchar() in
 * src/drivers/uart/common.c. The linker picks this over the weak stub.
 */
int plat_console_putchar(unsigned int c)
{
    unsigned int spins = 0;

    /*
     * Re-assert TX/RX mode and FIFO enable. ABL appears to park the port
     * (clear the UCON mode bits) on its way out -- exactly the state
     * exynos_port_configured() treats as "not configured". The clock is still
     * running (ABL used this UART), so this is NOT a clock op: we only turn the
     * transmitter back on. Cheap and idempotent, so we do it every call.
     */
    REG(UCON)  = (REG(UCON) & ~UCON_MODE_MASK) | UCON_TXRX_POLL;
    REG(UFCON) |= UFCON_FIFOMODE;

    if (REG(UFCON) & UFCON_FIFOMODE) {
        while (REG(UFSTAT) & UFSTAT_TXFULL) {
            if (++spins > TX_SPIN_LIMIT) {
                return -1;
            }
        }
    } else {
        while (!(REG(UTRSTAT) & UTRSTAT_TXE)) {
            if (++spins > TX_SPIN_LIMIT) {
                return -1;
            }
        }
    }

    REG(UTXH) = c & 0xff;
    return 0;
}
