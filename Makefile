# SPDX-License-Identifier: BSD-2-Clause

BOARD := tensor_g4
CONFIG := smp-debug

BUILD := $(abspath build)
MICROKIT := $(abspath deps/microkit)
SDK_VERSION = $(shell cat $(MICROKIT)/VERSION)-tensor-g4
SDK = $(MICROKIT)/release/microkit-sdk-$(SDK_VERSION)
SDK_LOADER = $(SDK)/board/$(BOARD)/$(CONFIG)/elf/loader.elf

DEPS_STAMP := $(BUILD)/deps.stamp
HELLO := $(BUILD)/hello
VMM := $(BUILD)/vmm

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

$(VMM)/loader.img: $(SDK_LOADER) FORCE
	mkdir -p $(VMM)
	$(MAKE) -C $(VMM) -f $(CURDIR)/vmm/vmm.mk TOP=$(CURDIR) MICROKIT_SDK=$(SDK) \
		MICROKIT_BOARD=$(BOARD) MICROKIT_CONFIG=$(CONFIG) \
		LIBVMM=$(abspath deps/libvmm) SDDF=$(abspath deps/sddf) \
		PREFIX_MAP_CFLAGS='$(PREFIX_MAP_CFLAGS)'

$(BUILD)/boot.img: $(VMM)/loader.img tools/mkbootimg.sh tools/arm64-image.py
	tools/mkbootimg.sh $< $@

hello: $(DEPS_STAMP) $(HELLO)/loader.img tools/mkbootimg.sh tools/arm64-image.py
	tools/mkbootimg.sh $(HELLO)/loader.img $(BUILD)/hello.img

FORCE:

clean:
	rm -rf $(BUILD)
