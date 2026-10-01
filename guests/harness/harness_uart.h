/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <stdint.h>

void uart_putc(char c);
void uart_puts(const char *s);
void uart_puthex32(uint32_t val);
void uart_puthex64(uint64_t val);
void uart_putdec(uint64_t val);
