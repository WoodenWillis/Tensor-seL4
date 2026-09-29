/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

/* samsung,exynos-uart: include/linux/serial_s3c.h */
#define EXYNOS_UART_UTRSTAT       0x10
#define EXYNOS_UART_UTXH          0x20

#define EXYNOS_UART_UTRSTAT_TXFE  (1u << 1)
#define EXYNOS_UART_UTRSTAT_TXE   (1u << 2)
