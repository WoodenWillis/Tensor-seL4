/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#define GUEST_NAME                  "linux"

#define GUEST_RAM_GPA               0x80000000
#define GUEST_RAM_SIZE              0x10000000

#define GUEST_INITRD_GPA            0x8d000000
#define GUEST_DTB_GPA               0x8f000000

#define GUEST_UART_GPA              0x10870000
#define GUEST_UART_SIZE             0x100

#define GUEST_GICD_GPA              0x10400000
#define GUEST_GICR_GPA              0x10440000
