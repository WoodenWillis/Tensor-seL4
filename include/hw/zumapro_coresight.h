/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

/* caiman DTB, google,exynos-coresight: dbg_base and pmu_base, one per core */
#define CS_NUM_CORES            8u
#define CS_DBG_PHYS(n)          (0x2b090000u + (n) * 0x80000u)
#define CS_PMU_PHYS(n)          (0x2b0a0000u + (n) * 0x80000u)

#define CS_VADDR_BASE           0x50000000u
#define CS_DBG_VADDR(n)         (CS_VADDR_BASE + (n) * 0x2000u)
#define CS_PMU_VADDR(n)         (CS_VADDR_BASE + (n) * 0x2000u + 0x1000u)

/* exynos-coresight.c core_regs.h */
#define CS_DBGOSLAR             0x300
#define CS_DBGPRSR              0x314
#define CS_MIDR                 0xd00
#define CS_DBGLAR               0xfb0
#define CS_PMUPCSR              0x200

#define CS_OSLOCK_MAGIC         0xc5acce55u
#define CS_LOCK                 0x1u
#define CS_PRSR_POWER_UP        (1u << 0)
#define CS_PRSR_RESET_STATE     (1u << 2)

#define CS_PCSR_NS(v)           (((v) >> 63) & 0x1u)
#define CS_PCSR_EL(v)           (((v) >> 61) & 0x3u)
#define CS_PCSR_SAMPLES         5u
