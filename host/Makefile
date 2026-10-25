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
# TEST selects stmlib's portable code paths (no ARM inline assembly).
FLAGS := -O2 -g -DTEST -DM_PI=3.14159265358979323846 -Wall -Wno-unused-parameter \
         -Wno-unused-private-field -Wno-unknown-attributes -Wno-unused-local-typedef $(INC)
OUT := build/$(UNIT)
# xcrun can hand out a Command Line Tools SDK newer than Xcode's linker
# understands; build against the SDK of the selected developer directory.
SYSROOT ?= $(shell xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
FLAGS += -isysroot $(SYSROOT)

$(OUT)/harness: harness.cc $(SRCS) $(wildcard $(PROJECT_ROOT)/*.h) Makefile
	@mkdir -p $(OUT)
	@for f in $(filter %.c,$(SRCS)); do \
	  clang -std=c11 $(FLAGS) -c $$f -o $(OUT)/$$(basename $$f .c).o || exit 1; done
	@for f in harness.cc $(filter %.cc,$(SRCS)); do \
	  clang++ -std=c++11 $(FLAGS) -c $$f -o $(OUT)/$$(basename $$f .cc).o || exit 1; done
	@clang++ -isysroot $(SYSROOT) $(OUT)/*.o -o $@
	@echo built $@

clean:
	rm -rf build
