# ProTracker for Tracker+ - top-level Makefile
#
#   make          build the Tracker+ firmware image (build/TrackerPlus_<version>.ptf)
#   make clean    remove build outputs
#   make version  print the firmware version

ROOT_DIR      := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ifneq ($(wildcard $(ROOT_DIR)/agt/10.3/bin/arm-none-eabi-gcc),)
  TOOLCHAIN   ?= $(ROOT_DIR)/agt/10.3/bin/arm-none-eabi-
else
  TOOLCHAIN   ?= arm-none-eabi-
endif

NPROC         ?= $(shell nproc 2>/dev/null || echo 4)
CORE_DIR      := $(ROOT_DIR)/core
IMX_DIR       := $(ROOT_DIR)/platforms/imx
SYNTH_DIR     := $(ROOT_DIR)/synth
UTIL_DIR      := $(ROOT_DIR)/util
OUT_DIR       := $(ROOT_DIR)/build

include $(ROOT_DIR)/version.mk

PLUS_POSTFIX  ?= $(EDITION_NAME)_$(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH)

.DEFAULT_GOAL := plus

.PHONY: all plus clean version bump-patch bump-minor

all: plus

plus:
	@echo "=================================================================="
	@echo " Building ProTracker for Tracker+ (IMX) - $(EDITION_NAME) $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(IMX_DIR)/build-PLUS/obj/source/PlatformImpl.o"
	@# the version is a -D flag, which make does not track: when it changes, rebuild what uses it
	@if [ "$$(cat "$(OUT_DIR)/.fwver" 2>/dev/null)" != "$(PLUS_POSTFIX)" ]; then \
		touch "$(CORE_DIR)/source/mtStructs.h"; mkdir -p "$(OUT_DIR)"; echo "$(PLUS_POSTFIX)" > "$(OUT_DIR)/.fwver"; fi
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=IMX PRODUCT=PLUS \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(IMX_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)" \
		PRODUCT=PLUS \
		TARGET_POSTFIX="$(PLUS_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(OUT_DIR)"
	@cp "$(IMX_DIR)/build-PLUS/TrackerPlus_$(PLUS_POSTFIX).ptf" "$(OUT_DIR)/"
	@echo "-> Output: $(OUT_DIR)/TrackerPlus_$(PLUS_POSTFIX).ptf"

clean:
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=IMX PRODUCT=PLUS
	$(MAKE) -C "$(IMX_DIR)" clean PRODUCT=PLUS
	rm -rf "$(OUT_DIR)"

version:
	@echo "Edition      : $(EDITION_NAME) (Build $(BUILD_TAG))"
	@echo "Tracker+ (IMX) : $(EDITION_NAME) $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH) -> TrackerPlus_$(PLUS_POSTFIX).ptf"

bump-patch:
	@python3 -c 'import re; p="version.mk"; s=open(p).read(); \
	m = re.search(r"PLUS_VER_PATCH\s*:=\s*(\d+)", s); \
	s = re.sub(r"PLUS_VER_PATCH\s*:=\s*\d+", "PLUS_VER_PATCH := %d" % (int(m.group(1)) + 1), s); \
	open(p, "w").write(s); print("Bumped patch version")'

bump-minor:
	@python3 -c 'import re; p="version.mk"; s=open(p).read(); \
	m = re.search(r"PLUS_VER_MINOR\s*:=\s*(\d+)", s); \
	s = re.sub(r"PLUS_VER_MINOR\s*:=\s*\d+", "PLUS_VER_MINOR := %d" % (int(m.group(1)) + 1), s); \
	s = re.sub(r"PLUS_VER_PATCH\s*:=\s*\d+", "PLUS_VER_PATCH := 0", s); \
	open(p, "w").write(s); print("Bumped minor version (patch reset to 0)")'
