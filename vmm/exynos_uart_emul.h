/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool exynos_uart_emul_init(uintptr_t gpa, size_t size, const char *guest_name);
