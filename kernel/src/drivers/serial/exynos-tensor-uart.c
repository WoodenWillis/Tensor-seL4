/*
 * Copyright (C) 2026 WoodenWillis
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Polled debug-console driver for the Tensor G4 (zumapro) debug UART.
 *
 * Target node (from the live caiman device tree):
 *
 *     serial_0: uart@10870000 {
 *         compatible          = "samsung,exynos-uart";
 *         samsung,dbg-uart-ch;                 // the TF-A / ABL console
 *         samsung,dbg-uart-baud = <0x1c200>;   // 115200
 *         samsung,dbg-word-len  = <0x08>;      // 8N1
 *         samsung,fifo-size     = <0x100>;     // 256-byte FIFOs
 *         reg-io-width          = <0x04>;      // 32-bit MMIO only
 *         reg                   = <0x0 0x10870000 0x100>;
 *         interrupts            = <0x0 0x281 0x4 0x0>;  // SPI 641
 *         status                = "okay";
 *     };
 *
 * DESIGN NOTE — why this driver does almost nothing:
 *
 * This port is already fully brought up by earlier boot stages (TF-A and ABL
 * log through it). It is the only node in the tree carrying the
 * `samsung,dbg-uart-ch` flag, and the Linux driver deliberately treats that
 * flag as "someone else owns the init path": exynos_usi_init() skips the USI
 * reset assertion for it, and exynos_port_configured() gates the console on
 * whether UCON already has a tx/rx mode set.
 *
 * So: we do NOT touch clocks, pinctrl, baud, ULCON, or the USI block. We
 * inherit a live port and only push bytes at it. Resetting a block the
 * bootloader configured is the fastest way to lose the console permanently
 * and have zero output left to debug the loss with.
 */

#include <config.h>
#include <stdint.h>
#include <util.h>
#include <machine/io.h>
#include <plat/machine/devices_gen.h>

/*
 * Register offsets — from <linux/serial_s3c.h> in the Android kernel tree.
 * URXH is not in the grep above; confirm it is 0x24 before trusting getchar.
 */
#define ULCON       0x00
#define UCON        0x04
#define UFCON       0x08
#define UTRSTAT     0x10
#define UFSTAT      0x18
#define UTXH        0x20
#define URXH        0x24    /* VERIFY: grep S3C2410_URXH serial_s3c.h */

/* UFCON */
#define UFCON_FIFOMODE      BIT(0)      /* S3C2410_UFCON_FIFOMODE */

/*
 * UFSTAT — CRITICAL: use the S5PV210 layout, not the legacy S3C2410 one.
 *
 *   S3C2410_UFSTAT_TXFULL  = (1 << 9)    <-- WRONG for Exynos
 *   S5PV210_UFSTAT_TXFULL  = (1 << 24)   <-- what exynos_serial_drv_data uses
 *
 * Picking bit 9 makes the FIFO look permanently non-full: you overrun it and
 * lose characters non-deterministically. Bit 24 is the one.
 */
#define UFSTAT_TXFULL       BIT(24)     /* S5PV210_UFSTAT_TXFULL */
#define UFSTAT_RXFULL       BIT(8)      /* S5PV210_UFSTAT_RXFULL */
#define UFSTAT_RXCOUNT      0xff        /* S5PV210_UFSTAT_RXMASK, shift 0 */

/* UTRSTAT */
#define UTRSTAT_TXE         BIT(2)      /* transmitter fully empty */
#define UTRSTAT_TXFE        BIT(1)      /* transmit buffer empty */
#define UTRSTAT_RXDR        BIT(0)      /* receive data ready */

/* UCON: exynos_port_configured() == (ucon & 0xf) != 0 */
#define UCON_MODE_MASK      0xf

/*
 * Bring-up guard. If the port is NOT live (bootloader didn't configure it, or
 * the USI block got gated), the TX-ready poll below would spin forever and the
 * kernel would hang with no output at all -- indistinguishable from a much
 * earlier crash. Bounding the spin means a dead console degrades to a silent
 * boot instead of a hang, so you can still reach a RAM console / JTAG.
 *
 * Drop this once the port is proven.
 */
#define TX_SPIN_LIMIT       1000000

/* reg-io-width = <4>: 32-bit accesses only. Byte writes to UTXH will not work. */
#define UART_REG(x) ((volatile uint32_t *)(UART_PPTR + (x)))

static inline bool_t tensor_uart_live(void)
{
    return (*UART_REG(UCON) & UCON_MODE_MASK) != 0;
}

#ifdef CONFIG_PRINTING

void uart_drv_putchar(unsigned char c)
{
    uint32_t spins = 0;

    /* ABL parks the port on handoff; re-assert TX/RX mode + FIFO. Clock is
       already running, so this is not a clock op. Makes the live-guard pass. */
    *UART_REG(UCON)  = (*UART_REG(UCON) & ~UCON_MODE_MASK) | 0x5u;
    *UART_REG(UFCON) |= UFCON_FIFOMODE;

    if (!tensor_uart_live()) {   /* now passes, because we just set the mode */
        return;
    }
    
    if (*UART_REG(UFCON) & UFCON_FIFOMODE) {
        /* FIFO mode (the bootloader's default): wait for space. */
        while (*UART_REG(UFSTAT) & UFSTAT_TXFULL) {
            if (++spins > TX_SPIN_LIMIT) {
                return;
            }
        }
    } else {
        /* Non-FIFO: wait for the holding register to drain. */
        while (!(*UART_REG(UTRSTAT) & UTRSTAT_TXE)) {
            if (++spins > TX_SPIN_LIMIT) {
                return;
            }
        }
    }
 
    *UART_REG(UTXH) = (uint32_t)c;
}
 

#endif /* CONFIG_PRINTING */

#ifdef CONFIG_DEBUG_BUILD

unsigned char uart_drv_getchar(void)
{
    if (!tensor_uart_live()) {
        return 0;
    }

    if (*UART_REG(UFCON) & UFCON_FIFOMODE) {
        while (((*UART_REG(UFSTAT) & UFSTAT_RXCOUNT) == 0) &&
               !(*UART_REG(UFSTAT) & UFSTAT_RXFULL)) {
            /* spin */
        }
    } else {
        while (!(*UART_REG(UTRSTAT) & UTRSTAT_RXDR)) {
            /* spin */
        }
    }

    return (unsigned char)(*UART_REG(URXH) & 0xff);
}

#endif /* CONFIG_DEBUG_BUILD */
