/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdint.h>
#include <microkit.h>

#include <trace/console_ring.h>

#include "breadcrumb.h"
#include "channels.h"

void _sddf_putchar(char c);

uintptr_t console_ring_vaddr;

void _sddf_putchar(char c)
{
    struct console_ring *ring = (struct console_ring *)console_ring_vaddr;
    uint64_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);

    if (head - __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE) >= CONSOLE_RING_CAPACITY) {
        breadcrumb_console_wait();
    }
    while (head - __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE) >= CONSOLE_RING_CAPACITY) {
        microkit_notify(CH_TRACER);
        seL4_Yield();
    }
    ring->buf[head % CONSOLE_RING_CAPACITY] = c;
    __atomic_store_n(&ring->head, head + 1, __ATOMIC_RELEASE);
    if (c == '\n') {
        microkit_notify(CH_TRACER);
    }
}
