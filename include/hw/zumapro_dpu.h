/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

/* attempt 1 fbcon.c: DPU cal_9865, DECON0 and RDMA0 */
#define DECON0_PHYS             0x19470000
#define DPU_RDMA0_PHYS          0x19900000

/* attempt 1: ABL framebuffer, 16 MiB at 0xfac00000, 1280x2856x4 */
#define ABL_FB_PHYS             0xfac00000
#define ABL_FB_SIZE             0x1000000

#define DECON_GLOBAL_CON        0x0020
#define DECON_TRIG_CON          0x0030
#define DECON_SHD_REG_UP_REQ    0x0050

#define GLOBAL_CON_DECON_EN_F   (1u << 0)
#define GLOBAL_CON_DECON_EN     (1u << 1)
#define GLOBAL_CON_RUN_STATUS   (1u << 4)

#define TRIG_CON_HW_TRIG_EN     (1u << 0)
#define TRIG_CON_SW_TRIG_DET_EN (1u << 1)
#define TRIG_CON_HW_TRIG_MASK   (1u << 4)
#define TRIG_CON_SW_TRIG_EN     (1u << 8)

#define SHD_REG_UP_REQ_GLOBAL   (1u << 31)

#define RDMA_IMG_SIZE           0x001c
#define RDMA_BASEADDR_P0        0x0040
#define RDMA_SRC_STRIDE_0       0x0050

#define RDMA_IMG_SIZE_W(v)      ((v) & 0xffffu)
#define RDMA_IMG_SIZE_H(v)      (((v) >> 16) & 0xffffu)
#define RDMA_SRC_STRIDE_MASK    0x00ffffffu
