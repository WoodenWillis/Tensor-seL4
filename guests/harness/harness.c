/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdint.h>

#include "harness_map.h"
#include "harness_uart.h"
#include "lab.h"

/* PSCI_VERSION */
#define PSCI_VERSION_FID            0x84000000u
/* SMCCC_VERSION */
#define SMCCC_VERSION_FID           0x80000000u
/* SiP service, function 0 */
#define HARNESS_UNHANDLED_SMC_FID   0x82000000u

void harness_main(uint64_t mode);

static uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static uint64_t smc0(uint64_t fid)
{
    register uint64_t x0 asm("x0") = fid;

    asm volatile("smc #0"
                 : "+r"(x0)
                 :
                 : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14",
                   "x15", "x16", "x17", "memory");
    return x0;
}

static void regression(void)
{
    uart_puts("WTCON=");
    uart_puthex32(mmio_read32(HARNESS_WATCHDOG_GPA + HARNESS_WATCHDOG_WTCON));
    uart_puts("\n");
    uart_puts("PSCI_VERSION=");
    uart_puthex32((uint32_t)smc0(PSCI_VERSION_FID));
    uart_puts("\n");
    uart_puts("SMCCC_VERSION=");
    uart_puthex32((uint32_t)smc0(SMCCC_VERSION_FID));
    uart_puts("\n");
    (void)smc0(HARNESS_UNHANDLED_SMC_FID);
    uart_puts("resumed\n");
}

void harness_main(uint64_t mode)
{
    uart_puts("hello\n");
    if (mode != 0) {
        lab_run(mode);
    }
    regression();
}
