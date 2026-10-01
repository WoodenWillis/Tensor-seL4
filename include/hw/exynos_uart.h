/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

/* samsung,exynos-uart: include/linux/serial_s3c.h */
#define EXYNOS_UART_ULCON         0x00
#define EXYNOS_UART_UCON          0x04
#define EXYNOS_UART_UFCON         0x08
#define EXYNOS_UART_UTRSTAT       0x10
#define EXYNOS_UART_UERSTAT       0x14
#define EXYNOS_UART_UFSTAT        0x18
#define EXYNOS_UART_UTXH          0x20
#define EXYNOS_UART_URXH          0x24
#define EXYNOS_UART_UINTM         0x38

#define EXYNOS_UART_UFCON_FIFOMODE   (1u << 0)
#define EXYNOS_UART_UFSTAT_RXCOUNT   0xffu
#define EXYNOS_UART_UFSTAT_RXFULL    (1u << 8)
#define EXYNOS_UART_UFSTAT_TXFULL    (1u << 24)
#define EXYNOS_UART_UTRSTAT_RXDR     (1u << 0)
#define EXYNOS_UART_UTRSTAT_TXFE     (1u << 1)
#define EXYNOS_UART_UTRSTAT_TXE      (1u << 2)
