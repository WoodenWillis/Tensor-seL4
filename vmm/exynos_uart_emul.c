/* SPDX-License-Identifier: BSD-2-Clause */

#include <libvmm/libvmm.h>

#include <hw/exynos_uart.h>

#include "exynos_uart_emul.h"
#include "mmio_trace.h"

#define CONSOLE_LINE_MAX 256

struct exynos_uart_emul {
    uintptr_t gpa;
    char line[CONSOLE_LINE_MAX];
    size_t line_len;
};

static struct exynos_uart_emul uart;

static void console_flush(struct exynos_uart_emul *u)
{
    u->line[u->line_len] = '\0';
    printf("guest| %s\n", u->line);
    u->line_len = 0;
}

static void console_putc(struct exynos_uart_emul *u, char c)
{
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        console_flush(u);
        return;
    }
    if (u->line_len == CONSOLE_LINE_MAX - 1) {
        console_flush(u);
    }
    u->line[u->line_len++] = c;
}

static bool uart_read(struct exynos_uart_emul *u, size_t vcpu_id, size_t offset, size_t fsr,
                      seL4_UserContext *regs)
{
    uint32_t val;

    switch (offset) {
    case EXYNOS_UART_UTRSTAT:
        val = EXYNOS_UART_UTRSTAT_TXE | EXYNOS_UART_UTRSTAT_TXFE;
        break;
    default:
        LOG_VMM_ERR("exynos-uart: read of unemulated offset 0x%lx\n", offset);
        return false;
    }

    fault_emulate_write(regs, offset, fsr, val);
    mmio_trace(vcpu_id, regs, u->gpa + offset, fsr, 0, val);
    return true;
}

static bool uart_write(struct exynos_uart_emul *u, size_t vcpu_id, size_t offset, size_t fsr,
                       seL4_UserContext *regs)
{
    switch (offset) {
    case EXYNOS_UART_UTXH: {
        uint64_t val = fault_get_data(regs, fsr);
        mmio_trace(vcpu_id, regs, u->gpa + offset, fsr, 0, val);
        console_putc(u, (char)(val & 0xff));
        return true;
    }
    default:
        LOG_VMM_ERR("exynos-uart: write of unemulated offset 0x%lx\n", offset);
        return false;
    }
}

static bool uart_fault(size_t vcpu_id, size_t offset, size_t fsr, seL4_UserContext *regs, void *data)
{
    struct exynos_uart_emul *u = data;

    if (fault_is_write(fsr)) {
        return uart_write(u, vcpu_id, offset, fsr, regs);
    }
    return uart_read(u, vcpu_id, offset, fsr, regs);
}

bool exynos_uart_emul_init(uintptr_t gpa, size_t size)
{
    uart.gpa = gpa;
    uart.line_len = 0;
    return fault_register_vm_exception_handler(gpa, size, uart_fault, &uart);
}
