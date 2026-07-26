/*
 * Copyright 2026, WoodenWillis
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * It replaces the sel4test suite as what the root task runs
 */

#include <autoconf.h>
#include <sel4test-driver/gen_config.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sel4/sel4.h>
#include <vka/object.h>
#include <simple/simple.h>
#include <vspace/vspace.h>
#include <sel4utils/thread.h>
#include <sel4utils/thread_config.h>
#include <sel4utils/api.h>
#include <platsupport/time_manager.h>
#include <platsupport/ltimer.h>
#include <platsupport/io.h>
#include <utils/util.h>
#include <utils/time.h>

#include "test.h"
#include "timer.h"

extern struct driver_env env;

/* Entry point invoked from main.c on a bigger stack */
void *system_service_continued(void *arg);

#define RING_CAP     16u
#define IPC_ITERS    50000u
#define ITEM_MAGIC   0xC0FFEE0000ULL

typedef struct {
    volatile uint32_t head;      /* written by producer */
    volatile uint32_t tail;      /* written by consumer */
    uint64_t slot[RING_CAP];
} ring_t;

static ring_t g_ring;

typedef struct {
    seL4_CPtr ep;                /* producer <-> consumer endpoint */
    seL4_CPtr done;              /* consumer signals the root task here */
    seL4_CPtr self_tcb;          /* so the worker can park itself */
    volatile uint32_t count;     /* progress counter */
    volatile uint32_t mismatches;
} pc_ctx_t;

static pc_ctx_t g_prod;
static pc_ctx_t g_cons;

typedef struct {
    volatile char *target;       /* deliberately-unmapped address */
    seL4_CPtr done;              /* signalled once the fault is resolved */
    seL4_CPtr self_tcb;
    volatile int observed;       /* value read back after resolution */
} fault_ctx_t;

static fault_ctx_t g_fault;

static void producer_fn(void *a0, void *a1 UNUSED, void *ipc UNUSED)
{
    pc_ctx_t *c = (pc_ctx_t *) a0;
    for (uint32_t i = 0; i < IPC_ITERS; i++) {
        uint32_t h = g_ring.head;
        g_ring.slot[h % RING_CAP] = ITEM_MAGIC + i;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        g_ring.head = h + 1;

        /* Hand off via IPC and block for the reply -> sustained round trip */
        seL4_SetMR(0, i);
        seL4_Call(c->ep, seL4_MessageInfo_new(0, 0, 0, 1));
        c->count = i + 1;
    }
    seL4_TCB_Suspend(c->self_tcb);
}

static void consumer_fn(void *a0, void *a1 UNUSED, void *ipc UNUSED)
{
    pc_ctx_t *c = (pc_ctx_t *) a0;
    for (uint32_t i = 0; i < IPC_ITERS; i++) {
        seL4_Word badge;
        seL4_Recv(c->ep, &badge);
        uint32_t seq = (uint32_t) seL4_GetMR(0);

        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        uint32_t t = g_ring.tail;
        uint64_t v = g_ring.slot[t % RING_CAP];
        g_ring.tail = t + 1;
        if (v != ITEM_MAGIC + seq) {
            c->mismatches++;
        }
        c->count = i + 1;

        seL4_Reply(seL4_MessageInfo_new(0, 0, 0, 0));   /* unblock producer */
    }
    seL4_Signal(c->done);
    seL4_TCB_Suspend(c->self_tcb);
}

static void faulter_fn(void *a0, void *a1 UNUSED, void *ipc UNUSED)
{
    fault_ctx_t *c = (fault_ctx_t *) a0;
    /* Deliberate page fault: the address is reserved but not backed */
    *(c->target) = 0x42;
    c->observed = *(c->target);
    seL4_Signal(c->done);
    seL4_TCB_Suspend(c->self_tcb);
}

/* Timer sleep/wake */

static volatile bool s_timer_fired;

static int sleep_cb(uintptr_t token UNUSED)
{
    s_timer_fired = true;
    return 0;
}

static void service_sleep_ns(uint64_t ns)
{
    int error;
    s_timer_fired = false;

    error = tm_alloc_id_at(&env.tm, TIMER_ID);
    ZF_LOGF_IF(error, "tm_alloc_id_at failed");
    error = tm_register_cb(&env.tm, TIMEOUT_RELATIVE, ns, 0, TIMER_ID, sleep_cb, 0);
    ZF_LOGF_IF(error, "tm_register_cb failed");

    while (!s_timer_fired) {
        wait_for_timer_interrupt(&env);
        error = tm_update(&env.tm);
        ZF_LOGF_IF(error, "tm_update failed");
    }

    tm_free_id(&env.tm, TIMER_ID);
    ltimer_reset(&env.ltimer);
}

/* ------------------------------------------------------------------ */

static int configure_worker(sel4utils_thread_t *t, seL4_CPtr fault_ep, uint8_t prio,
                            seL4_CNode cspace, seL4_Word cspace_data)
{
    sel4utils_thread_config_t cfg =
        thread_config_default(&env.simple, cspace, cspace_data, fault_ep, prio);
    return sel4utils_configure_thread_config(&env.vka, &env.vspace, &env.vspace, cfg, t);
}

/* Framebuffer console */
extern int fbcon_init(void);

static void run_system_service(void)
{
    int error;
    const uint8_t wprio = seL4_MaxPrio - 1;
    const seL4_CNode cspace = simple_get_cnode(&env.simple);
    const seL4_Word cspace_data =
        api_make_guard_skip_word(seL4_WordBits - simple_get_cnode_size_bits(&env.simple));

    /* Bring up the on-screen console first so everything below is mirrored to
     * the panel as well as the UART. */
    if (fbcon_init() == 0) {
        printf("[fbcon ] framebuffer console up: logs now mirrored to the panel\n");
    } else {
        printf("[fbcon ] framebuffer console init failed; UART only\n");
    }

    printf("\n==================================================\n");
    printf(" tensor-g4 system service\n");
    printf("==================================================\n");

    /* ---- 1. Timer sleep/wake ------------------------------------- */
    uint64_t t0 = timestamp(&env);
    service_sleep_ns(100 * NS_IN_MS);
    uint64_t t1 = timestamp(&env);
    printf("[timer ] slept 100ms; measured %llu us elapsed\n",
           (unsigned long long)((t1 - t0) / 1000));

    /* ---- 2. Deliberate page fault, caught and resolved ----------- */
    void *fvaddr = NULL;
    reservation_t rsv = vspace_reserve_range(&env.vspace, BIT(seL4_PageBits),
                                             seL4_AllRights, 1, &fvaddr);
    ZF_LOGF_IF(rsv.res == NULL, "failed to reserve fault range");

    vka_object_t fault_ep = {0};
    vka_object_t fault_done = {0};
    error = vka_alloc_endpoint(&env.vka, &fault_ep);
    ZF_LOGF_IF(error, "alloc fault endpoint");
    error = vka_alloc_notification(&env.vka, &fault_done);
    ZF_LOGF_IF(error, "alloc fault notification");

    g_fault.target = (volatile char *) fvaddr;
    g_fault.done = fault_done.cptr;

    sel4utils_thread_t faulter;
    error = configure_worker(&faulter, fault_ep.cptr, wprio, cspace, cspace_data);
    ZF_LOGF_IF(error, "configure faulter");
    g_fault.self_tcb = faulter.tcb.cptr;

    printf("[fault ] faulter will store to unmapped %p ...\n", fvaddr);
    error = sel4utils_start_thread(&faulter, faulter_fn, &g_fault, NULL, 1);
    ZF_LOGF_IF(error, "start faulter");

    /* Root task is the fault handler */
    seL4_Word badge;
    seL4_MessageInfo_t info = seL4_Recv(fault_ep.cptr, &badge);
    if (seL4_MessageInfo_get_label(info) == seL4_Fault_VMFault) {
        seL4_Word ip = seL4_GetMR(seL4_VMFault_IP);
        seL4_Word addr = seL4_GetMR(seL4_VMFault_Addr);
        printf("[fault ] caught VMFault addr=%p ip=%p; backing page and resuming\n",
               (void *) addr, (void *) ip);
        error = vspace_new_pages_at_vaddr(&env.vspace, fvaddr, 1, seL4_PageBits, rsv);
        ZF_LOGF_IF(error, "failed to map page for fault");
        seL4_Reply(seL4_MessageInfo_new(0, 0, 0, 0));   /* restart the store */
    } else {
        printf("[fault ] unexpected fault label %lu\n",
               (unsigned long) seL4_MessageInfo_get_label(info));
    }
    seL4_Wait(fault_done.cptr, &badge);
    printf("[fault ] faulter resumed and read back 0x%x -- fault resolved\n",
           g_fault.observed);

    /* ---- 3. Producer/consumer: shared ring + sustained IPC ------- */
    vka_object_t ep = {0};
    vka_object_t done = {0};
    error = vka_alloc_endpoint(&env.vka, &ep);
    ZF_LOGF_IF(error, "alloc pc endpoint");
    error = vka_alloc_notification(&env.vka, &done);
    ZF_LOGF_IF(error, "alloc pc notification");

    g_ring.head = g_ring.tail = 0;
    g_prod = (pc_ctx_t) { .ep = ep.cptr };
    g_cons = (pc_ctx_t) { .ep = ep.cptr, .done = done.cptr };

    sel4utils_thread_t prod, cons;
    error = configure_worker(&cons, 0, wprio, cspace, cspace_data);
    ZF_LOGF_IF(error, "configure consumer");
    g_cons.self_tcb = cons.tcb.cptr;
    error = configure_worker(&prod, 0, wprio, cspace, cspace_data);
    ZF_LOGF_IF(error, "configure producer");
    g_prod.self_tcb = prod.tcb.cptr;

    printf("[ipc   ] %u items through a %u-slot ring with round-trip IPC ...\n",
           IPC_ITERS, RING_CAP);
    uint64_t s0 = timestamp(&env);
    error = sel4utils_start_thread(&cons, consumer_fn, &g_cons, NULL, 1);
    ZF_LOGF_IF(error, "start consumer");
    error = sel4utils_start_thread(&prod, producer_fn, &g_prod, NULL, 1);
    ZF_LOGF_IF(error, "start producer");

    seL4_Wait(done.cptr, &badge);
    uint64_t s1 = timestamp(&env);
    uint64_t us = (s1 - s0) / 1000;

    printf("[ipc   ] done: produced=%u consumed=%u mismatches=%u\n",
           g_prod.count, g_cons.count, g_cons.mismatches);
    if (us > 0) {
        printf("[ipc   ] %u round-trips in %llu us (~%llu IPC/s)\n",
               IPC_ITERS, (unsigned long long) us,
               (unsigned long long)(((uint64_t) IPC_ITERS * 2u * 1000000u) / us));
    }

    printf("==================================================\n");
    printf(" system service complete: threads + ring + timer + fault + IPC\n");
    printf("==================================================\n");
    printf("------------------Fuck u Google-------------------\n");
}

void *system_service_continued(void *arg UNUSED)
{
    run_system_service();
    return NULL;
}
