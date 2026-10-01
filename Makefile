# SPDX-License-Identifier: BSD-2-Clause

BOARD := tensor_g4
CONFIG := smp-debug
GUEST ?= harness

BUILD := $(abspath build)
MICROKIT := $(abspath deps/microkit)
SDK_VERSION = $(shell cat $(MICROKIT)/VERSION)-tensor-g4
SDK = $(MICROKIT)/release/microkit-sdk-$(SDK_VERSION)
SDK_LOADER = $(SDK)/board/$(BOARD)/$(CONFIG)/elf/loader.elf

DEPS_STAMP := $(BUILD)/deps.stamp
HELLO := $(BUILD)/hello
VMM := $(BUILD)/vmm-$(GUEST)
GKI_IMAGE := $(BUILD)/guest/Image
DEVICE_TXT := hw/dts/caiman-BP1A.250505.005.txt
DEVICE_DTB := hw/dts/caiman-BP1A.250505.005.dtb

CARGO_HOME ?= $(HOME)/.cargo
PREFIX_MAP_CFLAGS := -ffile-prefix-map=$(CURDIR)=.
PREFIX_MAP_RUSTFLAGS := --remap-path-prefix=$(CURDIR)=. --remap-path-prefix=$(CARGO_HOME)=/cargo

.PHONY: all deps sdk hello clean

all: $(DEPS_STAMP)
	$(MAKE) $(BUILD)/boot.img

deps: $(DEPS_STAMP)

sdk: $(SDK_LOADER)

$(DEPS_STAMP): manifest/default.xml $(wildcard patches/*/*.patch) tools/fetch-deps.sh
	tools/fetch-deps.sh
	mkdir -p $(@D)
	touch $@

$(SDK_LOADER): $(DEPS_STAMP) Makefile
	rm -rf $(SDK) $(MICROKIT)/build/$(BOARD) $(MICROKIT)/build/initialiser
	cd $(MICROKIT) && \
		CFLAGS='$(PREFIX_MAP_CFLAGS)' ASMFLAGS='$(PREFIX_MAP_CFLAGS)' \
		CARGO_TARGET_AARCH64_UNKNOWN_NONE_RUSTFLAGS='$(PREFIX_MAP_RUSTFLAGS)' \
		python3 build_sdk.py --sel4 ../seL4 --boards $(BOARD) \
		--configs $(CONFIG) --version $(SDK_VERSION) --skip-docs --skip-tar

$(HELLO)/loader.img: $(SDK_LOADER)
	rm -rf $(HELLO)
	mkdir -p $(HELLO)
	$(MAKE) -C $(MICROKIT)/example/hello BUILD_DIR=$(HELLO) MICROKIT_SDK=$(SDK) \
		MICROKIT_BOARD=$(BOARD) MICROKIT_CONFIG=$(CONFIG)

GUEST_DEPS_harness :=
GUEST_DEPS_linux := $(GKI_IMAGE)
GUEST_DEPS_lab := $(GKI_IMAGE)

$(GKI_IMAGE): tools/extract-gki.py
	python3 tools/extract-gki.py $@

$(VMM)/loader.img: $(SDK_LOADER) $(GUEST_DEPS_$(GUEST)) FORCE
	mkdir -p $(VMM)
	tools/gen-trace-stamp.sh $(DEVICE_TXT) $(DEVICE_DTB) flake.lock $(VMM)/trace_stamp.h
	$(MAKE) -C $(VMM) -f $(CURDIR)/vmm/vmm.mk TOP=$(CURDIR) MICROKIT_SDK=$(SDK) \
		MICROKIT_BOARD=$(BOARD) MICROKIT_CONFIG=$(CONFIG) \
		GUEST=$(GUEST) GUEST_KERNEL=$(GKI_IMAGE) \
		LIBVMM=$(abspath deps/libvmm) SDDF=$(abspath deps/sddf) \
		PREFIX_MAP_CFLAGS='$(PREFIX_MAP_CFLAGS)'

$(BUILD)/boot.img: $(VMM)/loader.img tools/mkbootimg.sh tools/arm64-image.py
	tools/mkbootimg.sh $< $@

hello: $(DEPS_STAMP) $(HELLO)/loader.img tools/mkbootimg.sh tools/arm64-image.py
	tools/mkbootimg.sh $(HELLO)/loader.img $(BUILD)/hello.img

FORCE:

clean:
	rm -rf $(BUILD)
