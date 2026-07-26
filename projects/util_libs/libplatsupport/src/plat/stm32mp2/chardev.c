/*
 * Copyright 2026, STMicroelectronics
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "../../chardev.h"
#include "../../common.h"
#include <utils/util.h>

#define UART_DEFN(devid) {         \
    .id      = UART##devid,         \
    .paddr   = UART##devid##_PADDR, \
    .size    = BIT(12),             \
    .init_fn = &uart_init           \
}

#define USART_DEFN(devid) {         \
    .id      = USART##devid,         \
    .paddr   = USART##devid##_PADDR, \
    .size    = BIT(12),             \
    .init_fn = &uart_init           \
}

static const struct dev_defn dev_defn[] = {
    USART_DEFN(1),
    USART_DEFN(2),
    USART_DEFN(3),
    USART_DEFN(6),
    UART_DEFN(4),
    UART_DEFN(5),
    UART_DEFN(7),
    UART_DEFN(8),
    UART_DEFN(9),
};

struct ps_chardevice *
ps_cdev_init(enum chardev_id id, const ps_io_ops_t *o, struct ps_chardevice *d)
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(dev_defn); i++) {
        if (dev_defn[i].id == id) {
            return (dev_defn[i].init_fn(dev_defn + i, o, d)) ? NULL : d;
        }
    }
    return NULL;
}
