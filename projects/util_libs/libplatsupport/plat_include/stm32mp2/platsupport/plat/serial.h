/*
 * Copyright 2026, STMicroelectronics
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define USART1_PADDR 0x40330000
#define USART2_PADDR 0x400e0000
#define USART3_PADDR 0x400f0000
#define USART6_PADDR 0x40220000
#define UART4_PADDR  0x40100000
#define UART5_PADDR  0x40110000
#define UART7_PADDR  0x40370000
#define UART8_PADDR  0x40380000
#define UART9_PADDR  0x402c0000

/* official device names */
enum chardev_id {
    USART1,
    USART2,
    USART3,
    USART6,
    UART4,
    UART5,
    UART7,
    UART8,
    UART9,
    /* defaults */
    PS_SERIAL_DEFAULT = USART2
};
