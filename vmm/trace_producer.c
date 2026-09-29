/* SPDX-License-Identifier: BSD-2-Clause */

#include "trace_producer.h"

static struct trace_ring *ring;
static microkit_channel tracer_ch;
static uint8_t producer_id;

static uint64_t read_cntpct(void)
{
    uint64_t val;
    asm volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(val));
    return val;
}

void trace_producer_init(uintptr_t ring_vaddr, microkit_channel ch, uint8_t id)
{
    ring = (struct trace_ring *)ring_vaddr;
    tracer_ch = ch;
    producer_id = id;
}

static void wait_for_space(uint64_t head)
{
    while (head - __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE) >= TRACE_RING_CAPACITY) {
        microkit_notify(tracer_ch);
        seL4_Yield();
    }
}

void trace_emit(struct trace_record *rec)
{
    uint64_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);

    wait_for_space(head);
    rec->seq = __atomic_fetch_add(&ring->next_seq, 1, __ATOMIC_RELAXED);
    rec->time = read_cntpct();
    rec->producer = producer_id;
    ring->records[head % TRACE_RING_CAPACITY] = *rec;
    __atomic_store_n(&ring->head, head + 1, __ATOMIC_RELEASE);
    microkit_notify(tracer_ch);
}
