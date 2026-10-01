/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include "harness_map.h"

#define GUEST_NAME                  "harness"

#define GUEST_NUM_VCPUS             1

#define GUEST_RAM_GPA               HARNESS_RAM_GPA
#define GUEST_RAM_SIZE              HARNESS_RAM_SIZE

#define GUEST_UART_GPA              HARNESS_UART_GPA
#define GUEST_UART_SIZE             HARNESS_UART_SIZE

#define GUEST_WATCHDOG_GPA          HARNESS_WATCHDOG_GPA
#define GUEST_WATCHDOG_SIZE         HARNESS_WATCHDOG_SIZE
