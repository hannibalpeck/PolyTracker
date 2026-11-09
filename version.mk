# ProTracker for Tracker+ - version configuration

EDITION_NAME := CE
VER_ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_TAG    ?= $(shell git -C "$(VER_ROOT_DIR)" rev-parse --short=7 HEAD 2>/dev/null || date +"%Y%m%d")

PLUS_VER_MAJOR     := 0
PLUS_VER_MINOR     := 9
PLUS_VER_PATCH     := 4
PLUS_VER_BETA      := 1

FW_VER_1    := $(PLUS_VER_MAJOR)
FW_VER_2    := $(PLUS_VER_MINOR)
FW_VER_3    := $(PLUS_VER_PATCH)
FW_BETA     := $(PLUS_VER_BETA)

FW_VERSION_DEFINES := -DFW_EDITION_NAME=\"$(EDITION_NAME)\" \
                      -DFW_VER_MAJOR=$(FW_VER_1) \
                      -DFW_VER_MINOR=$(FW_VER_2) \
                      -DFW_VER_PATCH=$(FW_VER_3) \
                      -DFW_VER_BETA=$(FW_BETA)

BUILD_ID_STR           := $(BUILD_TAG)
DEFAULT_TARGET_POSTFIX := $(EDITION_NAME)_$(FW_VER_1).$(FW_VER_2).$(FW_VER_3)
