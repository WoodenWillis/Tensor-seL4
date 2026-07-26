/*
 * Copyright 2026, WoodenWillis
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Early platform init for tensor-g4, run by the ELF-loader before the kernel
 * image is loaded and started (arch-arm/sys_boot.c calls platform_init()).
 */

#include <autoconf.h>
#include <printf.h>
#include <types.h>

#include <elfloader.h>

#define TENSOR_G4_WDT_ADDRS_CONFIRMED  1

#define WDT_CLUSTER0_BASE   0x10060000u   /* watchdog_cl0@10060000 */
#define WDT_CLUSTER1_BASE   0x10070000u   /* watchdog_cl1@10070000 */
#define PMU_BASE            0x15460000u   /* system-controller@15460000, samsung,gs101-pmu */

/* Samsung/Exynos watchdog block register (s3c2410-style). */
#define WDT_WTCON           0x00u
#define WDT_WTCON_ENABLE    (1u << 5)     /* watchdog timer enable */
#define WDT_WTCON_RSTEN     (1u << 0)     /* watchdog reset enable */

#define PMU_CL0_MASK_RESET  0x1244u       /* GS_CLUSTER0_NONCPU_INT_EN */
#define PMU_CL0_CNT_EN      0x1220u       /* GS_CLUSTER0_NONCPU_OUT    */
#define PMU_CL1_MASK_RESET  0x1264u       /* GS_CLUSTER1_NONCPU_INT_EN */
#define PMU_CL1_CNT_EN      0x1240u       /* GS_CLUSTER1_NONCPU_OUT    */
#define PMU_MASK_RESET_BIT  2u
#define PMU_CNT_EN_BIT      8u

static inline uint32_t reg_read(uintptr_t base, uintptr_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void reg_write(uintptr_t base, uintptr_t off, uint32_t val)
{
    *(volatile uint32_t *)(base + off) = val;
}

static void disable_one_watchdog(uintptr_t wdt_base, uintptr_t mask_reset_reg,
                                 uintptr_t cnt_en_reg)
{
#if TENSOR_G4_WDT_ADDRS_CONFIRMED
    uint32_t wtcon = reg_read(wdt_base, WDT_WTCON);
    reg_write(wdt_base, WDT_WTCON, wtcon & ~(WDT_WTCON_ENABLE | WDT_WTCON_RSTEN));

    uint32_t m = reg_read(PMU_BASE, mask_reset_reg);
    reg_write(PMU_BASE, mask_reset_reg, m & ~(1u << PMU_MASK_RESET_BIT));

    uint32_t c = reg_read(PMU_BASE, cnt_en_reg);
    reg_write(PMU_BASE, cnt_en_reg, c & ~(1u << PMU_CNT_EN_BIT));
#else
    (void)wdt_base;
    (void)mask_reset_reg;
    (void)cnt_en_reg;
#endif
}

void platform_init(void)
{
    disable_one_watchdog(WDT_CLUSTER0_BASE, PMU_CL0_MASK_RESET, PMU_CL0_CNT_EN);
    disable_one_watchdog(WDT_CLUSTER1_BASE, PMU_CL1_MASK_RESET, PMU_CL1_CNT_EN);
#if TENSOR_G4_WDT_ADDRS_CONFIRMED
    printf("tensor-g4: SoC cluster watchdogs disabled\n");
#else
    printf("tensor-g4: watchdog disable pending base-address confirmation\n");
#endif
}
