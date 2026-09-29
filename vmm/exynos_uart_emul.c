/* SPDX-License-Identifier: BSD-2-Clause */

#include <libvmm/libvmm.h>

#include <hw/exynos_uart.h>

#include "exynos_uart_emul.h"
#include "mmio_trace.h"

struct exynos_uart_emul {
    uintptr_t gpa;
};

static struct exynos_uart_emul uart;

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
    case EXYNOS_UART_UTXH:
        mmio_trace(vcpu_id, regs, u->gpa + offset, fsr, 0, fault_get_data(regs, fsr));
        return true;
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
    return fault_register_vm_exception_handler(gpa, size, uart_fault, &uart);
}
