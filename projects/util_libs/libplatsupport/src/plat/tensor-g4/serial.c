/*
 * Copyright 2026, WoodenWillis
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Userspace (libplatsupport) driver for the Tensor G4 debug UART @ 0x10870000.
 *
 * Same register contract as the kernel-side driver: the port is brought up by
 * TF-A/ABL and we inherit it live. We do NOT touch clocks, pinctrl, baud,
 * ULCON, or the USI block -- on this SoC we *cannot* (see plat/clock.h).
 */

#include <stdlib.h>
#include <string.h>
#include <platsupport/serial.h>
#include <platsupport/plat/serial.h>

#include "../../chardev.h"

/* Register offsets -- <linux/serial_s3c.h>, all verified against the tree. */
#define ULCON       0x00
#define UCON        0x04
#define UFCON       0x08
#define UTRSTAT     0x10
#define UFSTAT      0x18
#define UTXH        0x20
#define URXH        0x24

/* UFCON */
#define UFCON_FIFOMODE      BIT(0)      /* S3C2410_UFCON_FIFOMODE */

/*
 * UFSTAT -- CRITICAL: the S5PV210 layout, not the legacy S3C2410 one.
 *
 *   S3C2410_UFSTAT_TXFULL = (1 << 9)    <-- WRONG on Exynos
 *   S5PV210_UFSTAT_TXFULL = (1 << 24)   <-- what exynos_serial_drv_data uses
 *
 * Polling bit 9 makes the FIFO look permanently non-full: you overrun it and
 * lose characters non-deterministically.
 */
#define UFSTAT_TXFULL       BIT(24)     /* S5PV210_UFSTAT_TXFULL */
#define UFSTAT_RXFULL       BIT(8)      /* S5PV210_UFSTAT_RXFULL  -- VERIFY */
#define UFSTAT_RXCOUNT      0xff        /* S5PV210_UFSTAT_RXMASK  -- VERIFY */

/* UTRSTAT */
#define UTRSTAT_TXE         BIT(2)      /* transmitter fully empty */
#define UTRSTAT_RXDR        BIT(0)      /* receive data ready */

/* UCON: exynos_port_configured() == (ucon & 0xf) != 0 */
#define UCON_MODE_MASK      0xf

/* reg-io-width = <4>: 32-bit accesses only. Byte writes to UTXH do not work. */
#define UART_REG(d, x) ((volatile uint32_t *)((uintptr_t)(d)->vaddr + (x)))

static inline int uart_live(ps_chardevice_t *d)
{
    return (*UART_REG(d, UCON) & UCON_MODE_MASK) != 0;
}

int uart_putchar(ps_chardevice_t *d, int c)
{
    if (!uart_live(d)) {
        return -1;
    }

    if (*UART_REG(d, UFCON) & UFCON_FIFOMODE) {
        if (*UART_REG(d, UFSTAT) & UFSTAT_TXFULL) {
            return -1;      /* no space; caller retries */
        }
    } else {
        if (!(*UART_REG(d, UTRSTAT) & UTRSTAT_TXE)) {
            return -1;
        }
    }

    *UART_REG(d, UTXH) = (uint32_t)(c & 0xff);

    /* Cook LF into CRLF if the device asked for it. */
    if (c == '\n' && (d->flags & SERIAL_AUTO_CR)) {
        uart_putchar(d, '\r');
    }

    return c;
}

int uart_getchar(ps_chardevice_t *d)
{
    if (!uart_live(d)) {
        return -1;
    }

    if (*UART_REG(d, UFCON) & UFCON_FIFOMODE) {
        uint32_t ufstat = *UART_REG(d, UFSTAT);
        if (((ufstat & UFSTAT_RXCOUNT) == 0) && !(ufstat & UFSTAT_RXFULL)) {
            return -1;
        }
    } else {
        if (!(*UART_REG(d, UTRSTAT) & UTRSTAT_RXDR)) {
            return -1;
        }
    }

    return (int)(*UART_REG(d, URXH) & 0xff);
}

static void uart_handle_irq(ps_chardevice_t *d UNUSED)
{
    /*
     * Polled console. The DT gives us SPI 641 (INTID 673) if RX interrupts are
     * wanted later; that would mean unmasking via UINTM (0x38) and acking in
     * UINTP (0x30). Not needed for a debug console.
     */
}

int uart_init(const struct dev_defn *defn, const ps_io_ops_t *ops, ps_chardevice_t *dev)
{
    memset(dev, 0, sizeof(*dev));

    void *vaddr = chardev_map(defn, ops);
    if (vaddr == NULL) {
        return -1;
    }

    dev->id         = defn->id;
    dev->vaddr      = vaddr;
    dev->read       = &uart_read;
    dev->write      = &uart_write;
    dev->handle_irq = &uart_handle_irq;
    dev->irqs       = defn->irqs;
    dev->ioops      = *ops;
    dev->flags      = SERIAL_AUTO_CR;

    /*
     * Deliberately no hardware init. The bootloader configured this port and
     * we cannot reconfigure it anyway -- the clock path is firmware-mediated.
     * Resetting a block someone else brought up is the fastest way to lose the
     * console permanently and have zero output left to debug the loss with.
     */

    return 0;
}
