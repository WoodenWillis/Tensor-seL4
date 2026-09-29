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
	-I$(TOP)/guests/harness \
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

VMM_OBJS := vmm.o exynos_uart_emul.o mmio_trace.o images.o

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := --start-group -lmicrokit -Tmicrokit.ld libvmm.a libsddf_util_debug.a --end-group

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

harness:
	mkdir -p $@

vmm.o exynos_uart_emul.o mmio_trace.o: %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

images.o: $(TOP)/vmm/images.S harness/harness.bin
	$(CC) -c -x assembler-with-cpp -DGUEST_HARNESS_IMAGE_PATH=\"harness/harness.bin\" $(ARCH_FLAGS) $< -o $@

vmm.elf: $(VMM_OBJS) libvmm.a libsddf_util_debug.a
	$(LD) $(LDFLAGS) $(VMM_OBJS) $(LIBS) -o $@

loader.img: vmm.elf $(TOP)/vmm/caiman.system
	$(MICROKIT_TOOL) $(TOP)/vmm/caiman.system --search-path . --board $(MICROKIT_BOARD) \
		--config $(MICROKIT_CONFIG) -o $@ -r report.txt

include $(LIBVMM)/vmm.mk
include $(SDDF)/util/util.mk

-include $(VMM_OBJS:.o=.d) harness/*.d
