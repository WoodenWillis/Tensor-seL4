/*
 * Copyright 2026, WoodenWillis
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/*
 * Tensor G4 (zumapro) clocks are NOT directly programmable from EL1/EL2, so
 * there is no meaningful libplatsupport clock driver to write here. This stub
 * is the correct description of the hardware, not a placeholder.
 *
 * Why: on this SoC the CMU blocks are firmware-mediated. drivers/clk/samsung/
 * clk-zuma.c is only a registration shim -- it contains no MUX/DIV/GATE/PLL
 * tables at all. It delegates everything to Samsung's cal-if layer
 * (drivers/soc/google/cal-if/zuma/), which in turn marshals requests over a
 * mailbox to the ACPM firmware core, and uses arm_smccc_smc() for the
 * privileged register paths. Those go to EL3 (TF-A), which we do not control.
 *
 * Consequence for this port: nothing in the elfloader, the kernel, or the
 * serial driver may reconfigure a clock. The UART is inherited already-live
 * from the bootloader -- this is exactly why the DT flags the console port
 * with `samsung,dbg-uart-ch` and why the Linux driver's
 * exynos_port_configured() refuses to re-init it. Our drivers only push bytes.
 */

enum clk_id {
    CLK_MASTER,
    /* ----- */
    NCLOCKS
};

enum clock_gate {
    NCLKGATES
};
