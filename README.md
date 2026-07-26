## Overview

End-to-end bring-up of **seL4 on the Google Tensor G4 (`zumapro`, Pixel 9 Pro)**: from a kernel that wouldn't compile, to a phone that boots seL4, passes the full sel4test suite, stays up indefinitely, runs a custom multithreaded system service, and **renders its own log on the OLED via a from-scratch DECON display driver**.

## The journey (in order)

### 1. Writing the new platform
- Create the tensor-g4 directory in kernel/src/plat
- Write the config.cmake file and extract the device tree from the working device to write overlay-tensor-g4.dts
- For the heterogeneous architecture of the Tensor-G4 (1×X4 + 3×A720 + 4×A520), A55 is the common denominator so that's what I'm going with for the bring up.
- Almost all the necessary values necessary for the platform are present in the device tree. When it wasn't the information was found in the repository for the GKI kernel for Caimito Android 16.

### 1. Kernel compiles

- Register the GICv3 interrupt-controller and ARM generic timer as `seL4,kernel-devices` in the DT overlay so `hardware_gen.py` emits `GICD_PPTR`/`GICR_PPTR`/`KERNEL_TIMER_IRQ`.

### 2. Userspace links + timer works
- Provide a userspace `ltimer` for tensor-g4 using the ARM generic timer (`libplatsupport/src/plat/tensor-g4/ltimer.c`, delegating to `generic_ltimer.c`), and enable `KernelArmExportPCNTUser`/`PTMRUser`. Fixes the `ltimer_default_init` link error.

### 3. Boots to 131/131
- Add a `reserved-memory` node marking the firmware/secure DRAM carveouts (gsa / gxp / tpu / aoc) `no-map`, so seL4 never hands them out as RAM. Fixes the lone `FRAMEEXPORTS0001` failure → **131/131**.

### 4. Stays up indefinitely
- Disable the SoC per-cluster watchdogs from the ELF-loader via the always-on PMU (`elfloader-tool/src/plat/tensor-g4/platform_init.c`), reproducing Android's `s3c2410_wdt.soft_noboot`.
- Contain the one-shot **imprecise SError** the disable write produces by draining it (dsb + ESB) at the last instant before the kernel starts (`sys_boot.c`).

### 5. Custom system service (replaces the test suite)
- `system_service.c`: one root task exercising **multiple threads + a shared-memory ring buffer + timer sleep/wake + a deliberate page fault (caught & resolved) + sustained producer/consumer IPC** (~1.1M IPC/s, 0 mismatches).

### 6. Framebuffer console — logs on the Pixel's screen
- A real **Exynos DECON command-mode display driver** built by reading the bootloader's DPU config off the hardware: find the framebuffer via `RDMA_BASEADDR_P0`, map it, and push frames with the DECON `direct_on_off → shadow-update → wait RUN_STATUS → SW-trigger` sequence (`cal_9865`).
- `fbcon.c`: 8×8 font (3× scaled), scrolling console, and a `printf` tee (`sel4muslcsys_register_stdio_write_fn`) so the whole service log is mirrored to the panel **and** the UART.

## Files produced and/or modified

| File | What |
|---|---|
| `kernel/src/plat/tensor-g4/overlay-tensor-g4.dts` | kernel-devices (GIC/timer), reserved-memory carveouts |
| `kernel/src/plat/tensor-g4/config.cmake` | export PCNT/PTMR to userspace |
| `projects/util_libs/libplatsupport/src/plat/tensor-g4/ltimer.c` | userspace ARM generic ltimer |
| `tools/seL4/elfloader-tool/src/plat/tensor-g4/platform_init.c` | PMU watchdog disable |
| `tools/seL4/elfloader-tool/src/arch-arm/sys_boot.c` | pre-kernel SError drain |
| `projects/sel4test/apps/sel4test-driver/src/system_service.c` | the system service + display bring-up |
| `projects/sel4test/apps/sel4test-driver/src/fbcon.c` | framebuffer text console |
| `projects/sel4test/apps/sel4test-driver/src/main.c` | run the service instead of the tests |

## Verified on hardware

- Boots, **sel4test 131/131**, no watchdog reset.
- System service runs: threads, ring buffer, 100 ms timer (±73 µs), page-fault resolve, ~1.1M IPC/s.
- The service log renders on the Pixel 9 Pro (1280×2856, `0xAARRGGBB`).
