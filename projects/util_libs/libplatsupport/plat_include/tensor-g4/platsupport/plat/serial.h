/*
 * Copyright 2026, WoodenWillis
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/*
 * Tensor G4 (zumapro) debug console.
 *
 * From the live caiman device tree:
 *
 *   serial_0: uart@10870000 {
 *       compatible            = "samsung,exynos-uart";
 *       samsung,dbg-uart-ch;                  // the TF-A / ABL console
 *       samsung,dbg-uart-baud = <0x1c200>;    // 115200
 *       samsung,dbg-word-len  = <0x08>;       // 8N1
 *       samsung,fifo-size     = <0x100>;      // 256-byte FIFOs
 *       reg-io-width          = <0x04>;       // 32-bit MMIO only
 *       reg                   = <0x0 0x10870000 0x100>;
 *       interrupts            = <0x0 0x281 0x4 0x0>;
 *       status                = "okay";
 *   };
 *
 * IRQ: SPI 0x281 (641). GIC INTID = 641 + 32 = 673.
 *
 * This is the only UART node in the tree carrying `samsung,dbg-uart-ch`, and
 * the only one worth exposing. The other 24 serial nodes are USI-muxed
 * peripherals (Bluetooth, sensors, ...) and are `status = "disabled"` apart
 * from serial_18 @ 0x155d0000, which is Android's logging UART.
 */

#define UART0_PADDR     0x10870000
#define UART0_OFFSET    0x0
#define UART0_IRQ       673             /* SPI 641 + 32 */

enum chardev_id {
    UART0,
    /* Aliases */
    PS_SERIAL0 = UART0,
    /* Defaults */
    PS_SERIAL_DEFAULT = UART0
};

#define DEFAULT_SERIAL_PADDR        UART0_PADDR
#define DEFAULT_SERIAL_INTERRUPT    UART0_IRQ
