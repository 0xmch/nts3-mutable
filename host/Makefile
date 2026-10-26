# Host build of a unit for offline tests: make UNIT=resonator
# Produces build/<unit>/harness; see harness.cc for usage.

UNIT ?= resonator
LOGUE_SDK ?= $(HOME)/src/logue-sdk
PROJECT_ROOT := $(abspath ../units/$(UNIT))
COMMON := $(LOGUE_SDK)/platform/nts-3_kaoss/common

include $(PROJECT_ROOT)/config.mk

SRCS := $(addprefix $(PROJECT_ROOT)/,$(filter-out /%,$(UCSRC) $(UCXXSRC))) \
        $(filter /%,$(UCSRC) $(UCXXSRC))
INC := -include stubs/host_attributes.h -I$(PROJECT_ROOT) -Istubs -I$(COMMON) $(addprefix -I,$(UINCDIR))
# UWASMDEFS are the unit's portable defines (TEST selects stmlib's code paths
# without ARM inline assembly), shared with the browser simulator build.
FLAGS := -O2 -g $(UWASMDEFS) -Wall -Wno-unused-parameter \
         -Wno-unused-private-field -Wno-unknown-attributes -Wno-unused-local-typedef $(INC)
OUT := build/$(UNIT)
# xcrun can hand out a Command Line Tools SDK newer than Xcode's linker
# understands; build against the SDK of the selected developer directory.
SYSROOT ?= $(shell xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
FLAGS += -isysroot $(SYSROOT)

HEADERS := $(shell find ../units ../third_party -name '*.h' -o -name '*.inc')

$(OUT)/harness: harness.cc $(SRCS) $(HEADERS) $(PROJECT_ROOT)/config.mk Makefile
	@mkdir -p $(OUT)
	@for f in $(filter %.c,$(SRCS)); do \
	  clang -std=c11 $(FLAGS) -c $$f -o $(OUT)/$$(basename $$f .c).o || exit 1; done
	@for f in harness.cc $(filter %.cc,$(SRCS)); do \
	  clang++ -std=c++11 $(FLAGS) -c $$f -o $(OUT)/$$(basename $$f .cc).o || exit 1; done
	@clang++ -isysroot $(SYSROOT) $(OUT)/*.o -o $@
	@echo built $@

clean:
	rm -rf build
