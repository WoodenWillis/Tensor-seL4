/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>
#include <sel4test-driver/gen_config.h>

#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>

#include <sel4runtime.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <cpio/cpio.h>

#include <platsupport/local_time_manager.h>

#include <sel4platsupport/timer.h>

#include <sel4debug/register_dump.h>
#include <sel4platsupport/device.h>
#include <sel4platsupport/platsupport.h>
#include <sel4utils/vspace.h>
#include <sel4utils/stack.h>
#include <sel4utils/process.h>
#include <sel4test/test.h>

#include <simple/simple.h>
#include <simple-default/simple-default.h>

#include <utils/util.h>

#include <vka/object.h>
#include <vka/capops.h>

#include <vspace/vspace.h>
#include "test.h"
#include "timer.h"

#include <sel4platsupport/io.h>

#define DRIVER_UNTYPED_MEMORY (1 << 25)

#define DRIVER_NUM_UNTYPEDS 16

#define ALLOCATOR_VIRTUAL_POOL_SIZE ((1 << seL4_PageBits) * 100)

#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 20)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

static sel4utils_alloc_data_t data;

struct driver_env env;
static vka_object_t untypeds[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
static uint8_t untyped_size_bits_list[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];

extern char _cpio_archive[];
extern char _cpio_archive_end[];

static elf_t tests_elf;

extern void *system_service_continued(void *arg);

static void init_env(driver_env_t env)
{
    allocman_t *allocman;
    reservation_t virtual_reservation;
    int error;

    /* create an allocator */
    allocman = bootstrap_use_current_simple(&env->simple, ALLOCATOR_STATIC_POOL_SIZE, allocator_mem_pool);
    if (allocman == NULL) {
        ZF_LOGF("Failed to create allocman");
    }

    allocman_make_vka(&env->vka, allocman);

    error = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&env->vspace,
                                                           &data, simple_get_pd(&env->simple),
                                                           &env->vka, platsupport_get_bootinfo());
    if (error) {
        ZF_LOGF("Failed to bootstrap vspace");
    }

    /* fill the allocator with virtual memory */
    void *vaddr;
    virtual_reservation = vspace_reserve_range(&env->vspace,
                                               ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_AllRights, 1, &vaddr);
    if (virtual_reservation.res == 0) {
        ZF_LOGF("Failed to provide virtual memory for allocator");
    }

    bootstrap_configure_virtual_pool(allocman, vaddr,
                                     ALLOCATOR_VIRTUAL_POOL_SIZE, simple_get_pd(&env->simple));

    error = sel4platsupport_new_io_ops(&env->vspace, &env->vka, &env->simple, &env->ops);
    ZF_LOGF_IF(error, "Failed to initialise IO ops");
}

static void free_objects(vka_object_t *objects, unsigned int num)
{
    for (unsigned int i = 0; i < num; i++) {
        vka_free_object(&env.vka, &objects[i]);
    }
}

static unsigned int allocate_untypeds(vka_object_t *untypeds, size_t bytes, unsigned int max_untypeds)
{
    unsigned int num_untypeds = 0;
    size_t allocated = 0;

    /* try to allocate as many of each possible untyped size as possible */
    for (uint8_t size_bits = seL4_WordBits - 1; size_bits > PAGE_BITS_4K; size_bits--) {
        while (num_untypeds < max_untypeds &&
               allocated + BIT(size_bits) <= bytes &&
               vka_alloc_untyped(&env.vka, size_bits, &untypeds[num_untypeds]) == 0) {
            allocated += BIT(size_bits);
            num_untypeds++;
        }
    }
    return num_untypeds;
}

/* extract a large number of untypeds from the allocator */
static unsigned int populate_untypeds(vka_object_t *untypeds)
{
    vka_object_t reserve[DRIVER_NUM_UNTYPEDS];
    unsigned int reserve_num = allocate_untypeds(reserve, DRIVER_UNTYPED_MEMORY, DRIVER_NUM_UNTYPEDS);

    unsigned int num_untypeds = allocate_untypeds(untypeds, UINT_MAX, ARRAY_SIZE(untyped_size_bits_list));
    for (unsigned int i = 0; i < num_untypeds; i++) {
        untyped_size_bits_list[i] = untypeds[i].size_bits;
    }

    free_objects(reserve, reserve_num);

    if (num_untypeds == 0) {
        ZF_LOGF("No untypeds for tests!");
    }

    return num_untypeds;
}

static void init_timer(void)
{
    if (config_set(CONFIG_HAVE_TIMER)) {
        int error;
        
	error = ltimer_default_init(&env.ltimer, env.ops, NULL, NULL);
        ZF_LOGF_IF(error, "Failed to setup the timers");

        error = vka_alloc_notification(&env.vka, &env.timer_notify_test);
        ZF_LOGF_IF(error, "Failed to allocate notification object for tests");

        error = seL4_TCB_BindNotification(simple_get_tcb(&env.simple), env.timer_notification.cptr);
        ZF_LOGF_IF(error, "Failed to bind timer notification to sel4test-driver");

        tm_init(&env.tm, &env.ltimer, &env.ops, 1);
    }
}

void sel4test_start_suite(const char *name)
{
    if (config_set(CONFIG_PRINT_XML)) {
        printf("<testsuite>\n");
    } else {
        printf("Starting test suite %s\n", name);
    }
}

void sel4test_start_test(const char *name, int n)
{
    if (config_set(CONFIG_PRINT_XML)) {
        printf("\t<testcase classname=\"%s\" name=\"%s\">\n", "sel4test", name);
    } else {
        printf("Starting test %d: %s\n", n, name);
    }
    sel4test_reset();
    sel4test_start_printf_buffer();
}

void sel4test_end_test(test_result_t result)
{
    sel4test_end_printf_buffer();
    test_check(result == SUCCESS);

    if (config_set(CONFIG_PRINT_XML)) {
        printf("\t</testcase>\n");
    }

    if (config_set(CONFIG_HAVE_TIMER)) {
        timer_reset(&env);
    }
}

void sel4test_end_suite(int num_tests, int num_tests_passed, int skipped_tests)
{
    if (config_set(CONFIG_PRINT_XML)) {
        printf("</testsuite>\n");
    } else {
        if (num_tests_passed != num_tests) {
            printf("Test suite failed. %d/%d tests passed.\n", num_tests_passed, num_tests);
        } else {
            printf("Test suite passed. %d tests passed. %d tests disabled.\n", num_tests, skipped_tests);
        }
    }
}

void sel4test_stop_tests(test_result_t result, int tests_done, int tests_failed, int num_tests, int skipped_tests)
{
    switch (result) {
    case ABORT:
        printf("Halting on fatal assertion...\n");
        break;
    case FAILURE:
        assert(config_set(CONFIG_TESTPRINTER_HALT_ON_TEST_FAILURE));
        printf("Halting on first test failure\n");
        break;
    default:
        break;
    }

    sel4test_start_test("Test all tests ran", num_tests + 1);
    test_eq(tests_done, num_tests);
    if (sel4test_get_result() != SUCCESS) {
        tests_failed++;
    }
    tests_done++;
    num_tests++;
    sel4test_end_test(sel4test_get_result());

    sel4test_end_suite(tests_done, tests_done - tests_failed, skipped_tests);

    if (tests_failed > 0) {
        printf("*** FAILURES DETECTED ***\n");
    } else if (tests_done < num_tests) {
        printf("*** ALL tests not run ***\n");
    } else {
        printf("All is well in the universe\n");
    }
    printf("\n\n");
}

static int collate_tests(testcase_t *tests_in, int n, testcase_t *tests_out[], int out_index,
                         regex_t *reg, int *skipped_tests)
{
    for (int i = 0; i < n; i++) {
        /* make sure the string is null terminated */
        tests_in[i].name[TEST_NAME_MAX - 1] = '\0';
        if (regexec(reg, tests_in[i].name, 0, NULL, 0) == 0) {
            if (tests_in[i].enabled) {
                tests_out[out_index] = &tests_in[i];
                out_index++;
            } else {
                (*skipped_tests)++;
            }
        }
    }

    return out_index;
}

void sel4test_run_tests(struct driver_env *e)
{
    /* Iterate through test types. */
    int max_test_types = (int)(__stop__test_type - __start__test_type);
    struct test_type *test_types[max_test_types];
    int num_test_types = 0;
    for (struct test_type *i = __start__test_type; i < __stop__test_type; i++) {
        test_types[num_test_types] = i;
        num_test_types++;
    }

    qsort(test_types, num_test_types, sizeof(struct test_type *), test_type_comparator);

    int driver_tests = (int)(__stop__test_case - __start__test_case);
    uint64_t tc_size = 0;
    testcase_t *sel4test_tests = (testcase_t *) sel4utils_elf_get_section(&tests_elf, "_test_case", &tc_size);
    if (sel4test_tests == NULL) {
        ZF_LOGF(TESTS_APP": Failed to find section: _test_case");
    }
    int tc_tests = tc_size / sizeof(testcase_t);
    int all_tests = driver_tests + tc_tests;
    testcase_t *tests[all_tests];

    regex_t reg;
    int error = regcomp(&reg, CONFIG_TESTPRINTER_REGEX, REG_EXTENDED | REG_NOSUB);
    ZF_LOGF_IF(error, "Error compiling regex \"%s\"", CONFIG_TESTPRINTER_REGEX);

    int skipped_tests = 0;
    int num_tests = collate_tests(__start__test_case, driver_tests, tests, 0, &reg, &skipped_tests);
    num_tests = collate_tests(sel4test_tests, tc_tests, tests, num_tests, &reg, &skipped_tests);

    regfree(&reg);

    qsort(tests, num_tests, sizeof(testcase_t *), test_comparator);

    for (int i = 1; i < num_tests; i++) {
        ZF_LOGF_IF(strcmp(tests[i]->name, tests[i - 1]->name) == 0, "tests have no strict order! %s %s",
                   tests[i]->name, tests[i - 1]->name);
    }

    int tests_done = 0;
    int tests_failed = 0;

    sel4test_start_suite("sel4test");
    sel4test_start_test("Test that there are tests", tests_done);
    test_gt(num_tests, 0);
    sel4test_end_test(sel4test_get_result());
    tests_done++;

    for (int tt = 0; tt < num_test_types; tt++) {
        if (test_types[tt]->set_up_test_type != NULL) {
            test_types[tt]->set_up_test_type((uintptr_t)e);
        }

        for (int i = 0; i < num_tests; i++) {
            if (tests[i]->test_type == test_types[tt]->id) {
                sel4test_start_test(tests[i]->name, tests_done);
                if (test_types[tt]->set_up != NULL) {
                    test_types[tt]->set_up((uintptr_t)e);
                }

                test_result_t result = test_types[tt]->run_test(tests[i], (uintptr_t)e);

                if (test_types[tt]->tear_down != NULL) {
                    test_types[tt]->tear_down((uintptr_t)e);
                }
                sel4test_end_test(result);

                if (result != SUCCESS) {
                    tests_failed++;
                    if (config_set(CONFIG_TESTPRINTER_HALT_ON_TEST_FAILURE) || result == ABORT) {
                        sel4test_stop_tests(result, tests_done + 1, tests_failed, num_tests + 1, skipped_tests);
                        return;
                    }
                }
                tests_done++;
            }
        }

        if (test_types[tt]->tear_down_test_type != NULL) {
            test_types[tt]->tear_down_test_type((uintptr_t)e);
        }
    }

    sel4test_stop_tests(SUCCESS, tests_done, tests_failed, num_tests + 1, skipped_tests);
}

void *main_continued(void *arg UNUSED)
{

    int num_elf_regions;
    sel4utils_elf_region_t elf_regions[MAX_REGIONS];

    unsigned long elf_size;
    unsigned long cpio_len = _cpio_archive_end - _cpio_archive;
    const void *elf_file = cpio_get_file(_cpio_archive, cpio_len, TESTS_APP, &elf_size);
    ZF_LOGF_IF(elf_file == NULL, "Error: failed to lookup ELF file");
    int status = elf_newFile(elf_file, elf_size, &tests_elf);
    ZF_LOGF_IF(status, "Error: invalid ELF file");

    printf("\n");
    printf("seL4 Test\n");
    printf("=========\n");
    printf("\n");

    int error;

    if (!config_set(CONFIG_PLAT_SPIKE)) {
        bool allocated = false;
        int untyped_count = simple_get_untyped_count(&env.simple);
        for (int i = 0; i < untyped_count; i++) {
            bool device = false;
            uintptr_t ut_paddr = 0;
            size_t ut_size_bits = 0;
            seL4_CPtr ut_cptr = simple_get_nth_untyped(&env.simple, i, &ut_size_bits, &ut_paddr, &device);
            if (device) {
                error = vka_alloc_frame_at(&env.vka, seL4_PageBits, ut_paddr, &env.device_obj);
                if (!error) {
                    allocated = true;
                    break;
                }
            }
        }
        ZF_LOGF_IF(allocated == false, "Failed to allocate a device frame for the frame tests");
    }

    /* allocate lots of untyped memory for tests to use */
    env.num_untypeds = populate_untypeds(untypeds);
    env.untypeds = untypeds;

    /* create a frame that will act as the init data, we can then map that
     * in to target processes */
    env.init = (test_init_data_t *) vspace_new_pages(&env.vspace, seL4_AllRights, 1, PAGE_BITS_4K);
    assert(env.init != NULL);

    memcpy(env.init->untyped_size_bits_list, untyped_size_bits_list, sizeof(uint8_t) * env.num_untypeds);

    num_elf_regions = sel4utils_elf_num_regions(&tests_elf);
    assert(num_elf_regions <= MAX_REGIONS);
    sel4utils_elf_reserve(&env.vspace, &tests_elf, elf_regions);

    memcpy(env.init->elf_regions, elf_regions, sizeof(sel4utils_elf_region_t) * num_elf_regions);
    env.init->num_elf_regions = num_elf_regions;
    
    env.init->priority = seL4_MaxPrio - 1;
    if (plat_init) {
        plat_init(&env);
    }

    /* Allocate a reply object for the RT kernel. */
    if (config_set(CONFIG_KERNEL_MCS)) {
        error = vka_alloc_reply(&env.vka, &env.reply);
        ZF_LOGF_IF(error, "Failed to allocate reply");
    }

    sel4test_run_tests(&env);

    return NULL;
}

#define MAX_ALLOC_AT_TO_TRACK 4
static vka_utspace_alloc_at_fn vka_utspace_alloc_at_base;
static bool serial_utspace_record = false;

typedef struct uspace_alloc_at_args {
    uintptr_t paddr;
    seL4_Word type;
    seL4_Word size_bits;
    cspacepath_t dest;
} uspace_alloc_at_args_t;
static int serial_utspace_alloc_at_fn(void *data, const cspacepath_t *dest, seL4_Word type, seL4_Word size_bits,
                                      uintptr_t paddr, seL4_Word *cookie)
{
    static uspace_alloc_at_args_t args_prev[MAX_ALLOC_AT_TO_TRACK] = {};
    static size_t num_alloc = 0;

    ZF_LOGF_IF(!vka_utspace_alloc_at_base, "vka_utspace_alloc_at_base not initialised.");
    if (!serial_utspace_record) {
        for (int i = 0; i < num_alloc; i++) {
            if (paddr == args_prev[i].paddr &&
                type == args_prev[i].type &&
                size_bits == args_prev[i].size_bits) {
                return vka_cnode_copy(dest, &args_prev[i].dest, seL4_AllRights);
            }
        }
        return vka_utspace_alloc_at_base(data, dest, type, size_bits, paddr, cookie);
    } else {
        ZF_LOGF_IF(num_alloc >= MAX_ALLOC_AT_TO_TRACK, "Trying to allocate too many utspace objects");
        int ret = vka_utspace_alloc_at_base(data, dest, type, size_bits, paddr, cookie);
        if (ret) {
            return ret;
        }
        uspace_alloc_at_args_t a = {.paddr = paddr, .type = type, .size_bits = size_bits, .dest = *dest};
        args_prev[num_alloc] = a;
        num_alloc++;
        return ret;

    }
}

static ps_irq_register_fn_t irq_register_fn_copy;
static irq_id_t sel4test_timer_irq_register(UNUSED void *cookie, ps_irq_t irq, irq_callback_fn_t callback,
                                            void *callback_data)
{
    static int num_timer_irqs = 0;

    int error;

    ZF_LOGF_IF(!callback, "Passed in a NULL callback");

    ZF_LOGF_IF(num_timer_irqs >= MAX_TIMER_IRQS, "Trying to register too many timer IRQs");

    error = sel4platsupport_copy_irq_cap(&env.vka, &env.simple, &irq,
                                         &env.timer_irqs[num_timer_irqs].handler_path);
    ZF_LOGF_IF(error, "Failed to allocate IRQ handler");

    if (env.timer_notification.cptr == seL4_CapNull) {
        error = vka_alloc_notification(&env.vka, &env.timer_notification);
        ZF_LOGF_IF(error, "Failed to allocate notification object");
    }

    error = vka_cspace_alloc_path(&env.vka, &env.badged_timer_notifications[num_timer_irqs]);
    ZF_LOGF_IF(error, "Failed to allocate path for the badged notification");
    cspacepath_t root_notification_path = {0};
    vka_cspace_make_path(&env.vka, env.timer_notification.cptr, &root_notification_path);
    error = vka_cnode_mint(&env.badged_timer_notifications[num_timer_irqs], &root_notification_path,
                           seL4_AllRights, BIT(num_timer_irqs));
    ZF_LOGF_IF(error, "Failed to mint notification for timer");

    error = seL4_IRQHandler_SetNotification(env.timer_irqs[num_timer_irqs].handler_path.capPtr,
                                            env.badged_timer_notifications[num_timer_irqs].capPtr);
    ZF_LOGF_IF(error, "Failed to pair the notification and handler together");

    error = seL4_IRQHandler_Ack(env.timer_irqs[num_timer_irqs].handler_path.capPtr);
    ZF_LOGF_IF(error, "Failed to ack the IRQ handler");

    env.timer_cbs[num_timer_irqs].callback = callback;
    env.timer_cbs[num_timer_irqs].callback_data = callback_data;

    return num_timer_irqs++;
}

/* When the root task exists, it should simply suspend itself */
static void sel4test_exit(int code)
{
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);
}

int main(void)
{
    sel4runtime_set_exit(sel4test_exit);

    int error;
    seL4_BootInfo *info = platsupport_get_bootinfo();

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(seL4_CapInitThreadTCB, "sel4test-driver");
#endif

    simple_default_init_bootinfo(&env.simple, info);

    init_env(&env);

    vka_utspace_alloc_at_base = env.vka.utspace_alloc_at;
    env.vka.utspace_alloc_at = serial_utspace_alloc_at_fn;

    /* enable serial driver */
    serial_utspace_record = true;
    platsupport_serial_setup_simple(&env.vspace, &env.simple, &env.vka);
    serial_utspace_record = false;

    irq_register_fn_copy = env.ops.irq_ops.irq_register_fn;
    env.ops.irq_ops.irq_register_fn = sel4test_timer_irq_register;
    init_timer();
    env.ops.irq_ops.irq_register_fn = irq_register_fn_copy;

    simple_print(&env.simple);

    printf("Switching to a safer, bigger stack... ");
    fflush(stdout);
    void *res;

    /* Run the tensor-g4 system service */
    error = sel4utils_run_on_stack(&env.vspace, system_service_continued, NULL, &res);
    test_assert_fatal(error == 0);
    test_assert_fatal(res == 0);

    return 0;
}
