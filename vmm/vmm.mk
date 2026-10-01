# SPDX-License-Identifier: BSD-2-Clause

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
ARCH := aarch64

CC := clang
LD := ld.lld
AR := llvm-ar
RANLIB := llvm-ranlib
OBJCOPY := llvm-objcopy
MICROKIT_TOOL := $(MICROKIT_SDK)/bin/microkit

SDDF_CUSTOM_LIBC := 1

ifeq ($(filter $(GUEST),harness linux lab),)
$(error GUEST must be harness, linux or lab)
endif

ifeq ($(strip $(CLANG_RESOURCE_DIR)),)
$(error CLANG_RESOURCE_DIR must be set; run inside the Nix dev shell)
endif

ARCH_FLAGS := -target aarch64-none-elf -mstrict-align -resource-dir $(CLANG_RESOURCE_DIR)

CFLAGS := \
	-ffreestanding \
	-g3 -O2 -Wall -Werror \
	-Wno-unused-function \
	-DBOARD_$(MICROKIT_BOARD) \
	$(PREFIX_MAP_CFLAGS) \
	-I$(BOARD_DIR)/include \
	-I$(LIBVMM)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/sddf/util/custom_libc \
	-I$(SDDF)/include/microkit \
	-I$(TOP)/include \
	-I$(TOP)/guests/$(GUEST) \
	-MD -MP \
	$(ARCH_FLAGS)

HARNESS_CFLAGS := \
	-ffreestanding -nostdlib -mgeneral-regs-only \
	-g3 -O2 -Wall -Werror \
	$(PREFIX_MAP_CFLAGS) \
	-I$(TOP)/include \
	-I$(TOP)/guests/harness \
	-MD -MP \
	$(ARCH_FLAGS)

LINUX_INIT_CFLAGS := \
	-ffreestanding -nostdlib -fno-pic -mgeneral-regs-only \
	-O2 -Wall -Werror \
	$(PREFIX_MAP_CFLAGS) \
	-MD -MP \
	$(ARCH_FLAGS)

GUEST_OBJS_harness := guest_image_harness.o images_harness.o
GUEST_OBJS_linux := guest_image_linux.o images_linux.o
GUEST_OBJS_lab := guest_image_lab.o guest_image_harness_lab.o guest_image_linux_lab.o images_harness.o images_linux.o

VMM_OBJS := vmm.o guest_control.o exynos_uart_emul.o mmio_forward.o mmio_trace.o smc_policy.o smc_trace.o trace_producer.o \
	breadcrumb.o guest_stats.o $(GUEST_OBJS_$(GUEST))
TRACER_OBJS := tracer.o
UARTRX_OBJS := uartrx.o trace_producer.o coresight.o

LDFLAGS := -L$(BOARD_DIR)/lib
VMM_LIBS := --start-group -lmicrokit -Tmicrokit.ld libvmm.a libsddf_util_console.a --end-group
TRACER_LIBS := --start-group -lmicrokit -Tmicrokit.ld libsddf_util_debug.a --end-group

vpath %.c $(TOP)/vmm $(LIBVMM)
vpath %.S $(TOP)/vmm

all: loader.img

harness/%.o: $(TOP)/guests/harness/%.c | harness
	$(CC) $(HARNESS_CFLAGS) -c -o $@ $<

harness/%.o: $(TOP)/guests/harness/%.S | harness
	$(CC) $(HARNESS_CFLAGS) -c -o $@ $<

harness/harness.ld: $(TOP)/guests/harness/harness.ld.S $(TOP)/guests/harness/harness_map.h | harness
	$(CC) -E -P -x c -I$(TOP)/guests/harness $< -o $@

HARNESS_OBJS := harness/start.o harness/harness.o harness/harness_uart.o harness/lab.o harness/vectors.o

harness/harness.elf: $(HARNESS_OBJS) harness/harness.ld
	$(LD) -T harness/harness.ld $(HARNESS_OBJS) -o $@

harness/harness.bin: harness/harness.elf
	$(OBJCOPY) -O binary $< $@

harness linux:
	mkdir -p $@

linux/init.o: $(TOP)/guests/linux/init.c | linux
	$(CC) $(LINUX_INIT_CFLAGS) -c -o $@ $<

linux/init.elf: linux/init.o
	$(LD) -static -e _start $< -o $@

linux/initrd.cpio: linux/init.elf $(TOP)/tools/mkcpio.py
	python3 $(TOP)/tools/mkcpio.py $< $@

LINUX_DTBS_harness :=
LINUX_DTBS_linux := linux/caiman-vm-0.dtb
LINUX_DTBS_lab := linux/caiman-vm-0.dtb linux/caiman-vm-1.dtb linux/caiman-vm-2.dtb
LINUX_DTBS := $(LINUX_DTBS_$(GUEST))
LINUX_DTB_DEFINES := $(foreach n,0 1 2,$(if $(filter linux/caiman-vm-$(n).dtb,$(LINUX_DTBS)),-DGUEST_DTB$(n)_IMAGE_PATH=\"linux/caiman-vm-$(n).dtb\"))

linux/caiman-vm-%.dts: $(TOP)/guests/linux/caiman-vm.dts.S $(TOP)/guests/$(GUEST)/guest_map.h linux/initrd.cpio
	$(CC) -E -P -undef -x assembler-with-cpp -I$(TOP)/guests/$(GUEST) -DGUEST_BOOT_CPU=$* \
		-DGUEST_INITRD_SIZE=$$(stat -c%s linux/initrd.cpio) $< -o $@

linux/caiman-vm-%.dtb: linux/caiman-vm-%.dts
	dtc -I dts -O dtb -o $@ $<

vmm.o guest_control.o exynos_uart_emul.o mmio_forward.o mmio_trace.o smc_policy.o smc_trace.o trace_producer.o \
	breadcrumb.o guest_stats.o guest_image_$(GUEST).o console_putchar.o: %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

guest_image_harness_lab.o: $(TOP)/vmm/guest_image_harness.c
	$(CC) $(CFLAGS) -Dguest_image_load=guest_image_harness_load -Dguest_image_name=guest_image_harness_name -c -o $@ $<

guest_image_linux_lab.o: $(TOP)/vmm/guest_image_linux.c
	$(CC) $(CFLAGS) -Dguest_image_load=guest_image_linux_load -Dguest_image_name=guest_image_linux_name -c -o $@ $<

images_harness.o: $(TOP)/vmm/images_harness.S harness/harness.bin
	$(CC) -c -x assembler-with-cpp -DGUEST_HARNESS_IMAGE_PATH=\"harness/harness.bin\" $(ARCH_FLAGS) $< -o $@

images_linux.o: $(TOP)/vmm/images_linux.S $(GUEST_KERNEL) $(LINUX_DTBS) linux/initrd.cpio
	$(CC) -c -x assembler-with-cpp $(ARCH_FLAGS) \
		-DGUEST_KERNEL_IMAGE_PATH=\"$(GUEST_KERNEL)\" \
		$(LINUX_DTB_DEFINES) \
		-DGUEST_INITRD_IMAGE_PATH=\"linux/initrd.cpio\" \
		$< -o $@

vmm.elf: $(VMM_OBJS) libvmm.a libsddf_util_console.a
	$(LD) $(LDFLAGS) $(VMM_OBJS) $(VMM_LIBS) -o $@

tracer.o: $(TOP)/tracer/tracer.c trace_stamp.h
	$(CC) $(CFLAGS) -I. -c -o $@ $<

tracer.elf: $(TRACER_OBJS) libsddf_util_debug.a
	$(LD) $(LDFLAGS) $(TRACER_OBJS) $(TRACER_LIBS) -o $@

uartrx.o: $(TOP)/uartrx/uartrx.c
	$(CC) $(CFLAGS) -I$(TOP)/vmm -I$(TOP)/diag -c -o $@ $<

uartrx.elf: $(UARTRX_OBJS) libsddf_util_debug.a
	$(LD) $(LDFLAGS) $(UARTRX_OBJS) $(TRACER_LIBS) -o $@

ifeq ($(strip $(SPLEEN_FONT_DIR)),)
$(error SPLEEN_FONT_DIR must be set; run inside the Nix dev shell)
endif
FBCON_FONT ?= spleen-12x24
FBCON_SCALE ?= 2
FBCON_MARGIN_TOP ?= 160
FBCON_MARGIN_BOTTOM ?= 120
FBCON_MARGIN_SIDE ?= 32
FBCON_CFLAGS := -DFBCON_SCALE=$(FBCON_SCALE)u -DFBCON_MARGIN_TOP=$(FBCON_MARGIN_TOP)u \
	-DFBCON_MARGIN_BOTTOM=$(FBCON_MARGIN_BOTTOM)u -DFBCON_MARGIN_SIDE=$(FBCON_MARGIN_SIDE)u

fbcon_font.h: $(SPLEEN_FONT_DIR)/$(FBCON_FONT).bdf $(TOP)/tools/bdf2c.py FORCE_FLAGS
	python3 $(TOP)/tools/bdf2c.py $< $@

fbcon.o: $(TOP)/fbcon/fbcon.c fbcon_font.h FORCE_FLAGS
	$(CC) $(CFLAGS) $(FBCON_CFLAGS) -I. -c -o $@ $<

FORCE_FLAGS:

coresight.o: $(TOP)/diag/coresight.c
	$(CC) $(CFLAGS) -c -o $@ $<

freezewatch.o: $(TOP)/freezewatch/freezewatch.c
	$(CC) $(CFLAGS) -I$(TOP)/diag -c -o $@ $<

freezewatch.elf: freezewatch.o coresight.o libsddf_util_debug.a
	$(LD) $(LDFLAGS) freezewatch.o coresight.o $(TRACER_LIBS) -o $@

fbcon.elf: fbcon.o libsddf_util_debug.a
	$(LD) $(LDFLAGS) fbcon.o $(TRACER_LIBS) -o $@

caiman.system: $(TOP)/vmm/caiman.system.S $(TOP)/guests/$(GUEST)/guest_map.h $(TOP)/include/hw/zumapro_dpu.h
	$(CC) -E -P -x c -I$(TOP)/guests/$(GUEST) -I$(TOP)/include $< -o $@

loader.img: vmm.elf tracer.elf uartrx.elf fbcon.elf freezewatch.elf caiman.system
	$(MICROKIT_TOOL) caiman.system --search-path . --board $(MICROKIT_BOARD) \
		--config $(MICROKIT_CONFIG) -o $@ -r report.txt

include $(LIBVMM)/vmm.mk
include $(SDDF)/util/util.mk

libsddf_util_console.a: $(BASE_OBJS_LIBUTIL) console_putchar.o
	$(RM) $@
	$(AR) crv $@ $^
	$(RANLIB) $@

-include $(VMM_OBJS:.o=.d) $(TRACER_OBJS:.o=.d) uartrx.d fbcon.d freezewatch.d coresight.d harness/*.d linux/*.d
