#
# Copyright 2026, WoodenWillis
#
# SPDX-License-Identifier: GPL-2.0-only
#

declare_platform(tensor-g4 KernelPlatformTensorG4 PLAT_TENSOR_G4 KernelSel4ArchAarch64)

if(KernelPlatformTensorG4)
	declare_seL4_arch(aarch64)
	set(KernelArmCortexA520 ON)
	set(KernelArchArmV8a ON)
	set(KernelArmGicV3 ON)
	# Export the ARM generic timer's physical counter and physical timer
	# registers to user level. libplatsupport's generic ARM ltimer
	# (src/arch/arm/generic_ltimer.c) reads CNTPCT/CNTP_CVAL directly, so these
	# must be enabled for userspace (e.g. sel4test) to have a working timer.
	set(KernelArmExportPCNTUser ON CACHE BOOL "" FORCE)
	set(KernelArmExportPTMRUser ON CACHE BOOL "" FORCE)
	config_set(KernelARMPlatform ARM_PLAT "tensor-g4")
	list(APPEND KernelDTSList "tools/dts/tensor-g4.dts")
	List(APPEND KernelDTSList "src/plat/tensor-g4/overlay-tensor-g4.dts")
	declare_default_headers(
		TIMER_FREQUENCY 24576000
		MAX_IRQ 991
		NUM_PPI 32
		TIMER drivers/timer/arm_generic.h
		CLK_SHIFT 51u
		CLK_MAGIC 91625969u
		KERNEL_WCET 10u
		INTERRUPT_CONTROLLER arch/arm/arch/machine/gic_v3.h
	)
endif()

add_sources(
    DEP "KernelPlatformTensorG4"
    CFILES
        src/arch/arm/machine/gic_v3.c
        src/arch/arm/machine/l2c_nop.c
        src/drivers/serial/exynos-tensor-uart.c
)
