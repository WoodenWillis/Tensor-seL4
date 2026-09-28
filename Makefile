# SPDX-License-Identifier: BSD-2-Clause

BOARD := tensor_g4
CONFIG := debug

BUILD := $(abspath build)
MICROKIT := $(abspath deps/microkit)
SDK_VERSION = $(shell cat $(MICROKIT)/VERSION)-tensor-g4
SDK = $(MICROKIT)/release/microkit-sdk-$(SDK_VERSION)
SDK_LOADER = $(SDK)/board/$(BOARD)/$(CONFIG)/elf/loader.elf

DEPS_STAMP := $(BUILD)/deps.stamp
HELLO := $(BUILD)/hello

.PHONY: all deps sdk clean

all: $(DEPS_STAMP)
	$(MAKE) $(BUILD)/boot.img

deps: $(DEPS_STAMP)

sdk: $(SDK_LOADER)

$(DEPS_STAMP): manifest/default.xml $(wildcard patches/*/*.patch) tools/fetch-deps.sh
	tools/fetch-deps.sh
	mkdir -p $(@D)
	touch $@

$(SDK_LOADER): $(DEPS_STAMP)
	rm -rf $(SDK) $(MICROKIT)/build/$(BOARD)
	cd $(MICROKIT) && python3 build_sdk.py --sel4 ../seL4 --boards $(BOARD) \
		--configs $(CONFIG) --version $(SDK_VERSION) --skip-docs --skip-tar

$(HELLO)/loader.img: $(SDK_LOADER)
	mkdir -p $(HELLO)
	$(MAKE) -C $(MICROKIT)/example/hello BUILD_DIR=$(HELLO) MICROKIT_SDK=$(SDK) \
		MICROKIT_BOARD=$(BOARD) MICROKIT_CONFIG=$(CONFIG)

$(BUILD)/boot.img: $(HELLO)/loader.img tools/mkbootimg.sh tools/arm64-image.py
	tools/mkbootimg.sh $< $@

clean:
	rm -rf $(BUILD)
