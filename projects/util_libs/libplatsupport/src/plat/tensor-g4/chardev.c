/*
 * Copyright 2026, WoodenWillis
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Character device definitions for the Tensor G4 (zumapro).
 */

#include <platsupport/chardev.h>
#include <platsupport/plat/serial.h>

#include "../../chardev.h"
#include "../../common.h"

static const int UART0_irqs[] = { UART0_IRQ, -1 };

#define UART_DEFN(devid) {              \
    .id      = devid,                   \
    .paddr   = devid##_PADDR,           \
    .size    = BIT(12),                 \
    .irqs    = devid##_irqs,            \
    .init_fn = &uart_init               \
}

static const struct dev_defn dev_defn[] = {
    UART_DEFN(UART0),
};

struct ps_chardevice *ps_cdev_init(enum chardev_id id,
                                   const ps_io_ops_t *o,
                                   struct ps_chardevice *d)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(dev_defn); i++) {
        if (dev_defn[i].id == id) {
            return (dev_defn[i].init_fn(dev_defn + i, o, d)) ? NULL : d;
        }
    }
    return NULL;
}
