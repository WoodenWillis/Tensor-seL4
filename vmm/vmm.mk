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

ifeq ($(filter $(GUEST),harness linux),)
$(error GUEST must be harness or linux)
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

VMM_OBJS := vmm.o guest_control.o exynos_uart_emul.o mmio_forward.o mmio_trace.o smc_policy.o smc_trace.o trace_producer.o \
	breadcrumb.o guest_stats.o guest_image_$(GUEST).o images_$(GUEST).o
TRACER_OBJS := tracer.o
UARTRX_OBJS := uartrx.o trace_producer.o

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

harness/harness.elf: harness/start.o harness/harness.o harness/harness.ld
	$(LD) -T harness/harness.ld harness/start.o harness/harness.o -o $@

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

linux/caiman-vm.dts: $(TOP)/guests/linux/caiman-vm.dts.S $(TOP)/guests/linux/guest_map.h linux/initrd.cpio
	$(CC) -E -P -undef -x assembler-with-cpp -I$(TOP)/guests/linux \
		-DGUEST_INITRD_SIZE=$$(stat -c%s linux/initrd.cpio) $< -o $@

linux/caiman-vm.dtb: linux/caiman-vm.dts
	dtc -I dts -O dtb -o $@ $<

vmm.o guest_control.o exynos_uart_emul.o mmio_forward.o mmio_trace.o smc_policy.o smc_trace.o trace_producer.o \
	breadcrumb.o guest_stats.o guest_image_$(GUEST).o console_putchar.o: %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

images_harness.o: $(TOP)/vmm/images_harness.S harness/harness.bin
	$(CC) -c -x assembler-with-cpp -DGUEST_HARNESS_IMAGE_PATH=\"harness/harness.bin\" $(ARCH_FLAGS) $< -o $@

images_linux.o: $(TOP)/vmm/images_linux.S $(GUEST_KERNEL) linux/caiman-vm.dtb linux/initrd.cpio
	$(CC) -c -x assembler-with-cpp $(ARCH_FLAGS) \
		-DGUEST_KERNEL_IMAGE_PATH=\"$(GUEST_KERNEL)\" \
		-DGUEST_DTB_IMAGE_PATH=\"linux/caiman-vm.dtb\" \
		-DGUEST_INITRD_IMAGE_PATH=\"linux/initrd.cpio\" \
		$< -o $@

vmm.elf: $(VMM_OBJS) libvmm.a libsddf_util_console.a
	$(LD) $(LDFLAGS) $(VMM_OBJS) $(VMM_LIBS) -o $@

tracer.o: $(TOP)/tracer/tracer.c trace_stamp.h
	$(CC) $(CFLAGS) -I. -c -o $@ $<

tracer.elf: $(TRACER_OBJS) libsddf_util_debug.a
	$(LD) $(LDFLAGS) $(TRACER_OBJS) $(TRACER_LIBS) -o $@

uartrx.o: $(TOP)/uartrx/uartrx.c
	$(CC) $(CFLAGS) -I$(TOP)/vmm -c -o $@ $<

uartrx.elf: $(UARTRX_OBJS) libsddf_util_debug.a
	$(LD) $(LDFLAGS) $(UARTRX_OBJS) $(TRACER_LIBS) -o $@

ifeq ($(strip $(SPLEEN_FONT_DIR)),)
$(error SPLEEN_FONT_DIR must be set; run inside the Nix dev shell)
endif
FBCON_FONT ?= spleen-12x24

fbcon_font.h: $(SPLEEN_FONT_DIR)/$(FBCON_FONT).bdf $(TOP)/tools/bdf2c.py
	python3 $(TOP)/tools/bdf2c.py $< $@

fbcon.o: $(TOP)/fbcon/fbcon.c fbcon_font.h
	$(CC) $(CFLAGS) -I. -c -o $@ $<

fbcon.elf: fbcon.o libsddf_util_debug.a
	$(LD) $(LDFLAGS) fbcon.o $(TRACER_LIBS) -o $@

caiman.system: $(TOP)/vmm/caiman.system.S $(TOP)/guests/$(GUEST)/guest_map.h $(TOP)/include/hw/zumapro_dpu.h
	$(CC) -E -P -x c -I$(TOP)/guests/$(GUEST) -I$(TOP)/include $< -o $@

loader.img: vmm.elf tracer.elf uartrx.elf fbcon.elf caiman.system
	$(MICROKIT_TOOL) caiman.system --search-path . --board $(MICROKIT_BOARD) \
		--config $(MICROKIT_CONFIG) -o $@ -r report.txt

include $(LIBVMM)/vmm.mk
include $(SDDF)/util/util.mk

libsddf_util_console.a: $(BASE_OBJS_LIBUTIL) console_putchar.o
	$(RM) $@
	$(AR) crv $@ $^
	$(RANLIB) $@

-include $(VMM_OBJS:.o=.d) $(TRACER_OBJS:.o=.d) uartrx.d fbcon.d harness/*.d linux/*.d
