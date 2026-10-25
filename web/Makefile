# Standalone WebAssembly build of one unit for the web page:
#   make UNIT=resonator     (with emsdk's emcc on PATH)
# Writes units/<unit>.wasm. Uses the unit's config.mk for its sources, minus
# unit.cc (the device entry points), plus glue.cc.

UNIT ?= resonator
LOGUE_SDK ?= $(HOME)/src/logue-sdk
PROJECT_ROOT := $(abspath ../units/$(UNIT))
COMMON := $(LOGUE_SDK)/platform/nts-3_kaoss/common

include $(PROJECT_ROOT)/config.mk

ABS = $(foreach f,$(1),$(if $(filter /%,$(f)),$(f),$(PROJECT_ROOT)/$(f)))
CSRCS := $(call ABS,$(UCSRC))
CXXSRCS := $(filter-out %/unit.cc,$(call ABS,$(UCXXSRC))) glue.cc
# effect.h is found next to unit.cc, wherever the unit keeps its sources.
EFFECT_DIR := $(dir $(firstword $(filter %/unit.cc,$(call ABS,$(UCXXSRC)))))

FLAGS := -O3 $(UWASMDEFS) -include ../host/stubs/host_attributes.h \
         -I$(EFFECT_DIR) -I../host/stubs -I$(COMMON) $(addprefix -I,$(UINCDIR)) \
         -Wno-unknown-attributes -Wno-unused-local-typedef
LINK := -sSTANDALONE_WASM --no-entry -sINITIAL_MEMORY=8MB -sSTACK_SIZE=128KB \
        -sALLOW_MEMORY_GROWTH=0 -sFILESYSTEM=0

units/$(UNIT).wasm: $(CSRCS) $(CXXSRCS) $(wildcard $(EFFECT_DIR)*.h) Makefile
	@mkdir -p units build/$(UNIT)
	@for f in $(CSRCS); do emcc -std=c11 $(FLAGS) -c $$f -o build/$(UNIT)/$$(basename $$f .c).o || exit 1; done
	@for f in $(CXXSRCS); do emcc -std=c++17 -fno-exceptions -fno-rtti $(FLAGS) -c $$f -o build/$(UNIT)/$$(basename $$f .cc).o || exit 1; done
	@emcc $(LINK) build/$(UNIT)/*.o -o $@
	@echo built $@ $$(wc -c < $@) bytes
