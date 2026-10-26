##############################################################################
# Meta Mod: Warps cross-modulation and vocoder, for the NTS-3 kaoss pad kit.
# Sources live in ../metamod; this directory only picks the unit.
#

PROJECT := metamod
PROJECT_TYPE := genericfx
WARPS_SHIFTER := 0
# Warps' rate (third_party/eurorack/warps/dsp/rate.h): the NTS-3's own 48 kHz,
# for half the CPU load of Warps' native 96 kHz.
WARPS_SAMPLE_RATE ?= 48000

MI := $(PROJECT_ROOT)/../../third_party/eurorack
METAMOD := $(PROJECT_ROOT)/../metamod
WASMMAIN := $(METAMOD)/wasm.cc

UCSRC = $(METAMOD)/header.c

UCXXSRC = $(METAMOD)/unit.cc \
          $(MI)/warps/dsp/modulator.cc \
          $(MI)/warps/dsp/oscillator.cc \
          $(MI)/warps/dsp/vocoder.cc \
          $(MI)/warps/dsp/filter_bank.cc \
          $(MI)/warps/resources.cc \
          $(MI)/stmlib/dsp/units.cc \
          $(MI)/stmlib/utils/random.cc

UASMSRC =

UASMXSRC =

UINCDIR = $(MI)

ULIBDIR =

# The unit image is capped at about 32 KB: section GC, and C++ symbols stay
# out of the ELF dynamic symbol table (unit.cc exports the unit API).
ULIBS = -lm -Wl,--gc-sections

# stmlib uses M_PI, which strict -std=c++11 hides in newlib. Meta Mod never
# turns on Warps' easter egg, so Modulator::Process leaves it out
# (warps/dsp/modulator.h).
WARPS_DEFS = -DWARPS_SHIFTER=$(WARPS_SHIFTER) -DWARPS_SAMPLE_RATE=$(WARPS_SAMPLE_RATE) -DWARPS_PROCESS_EASTER_EGG=0
UDEFS = -DM_PI=3.14159265358979323846 $(WARPS_DEFS)
UDEFS += -ffunction-sections -fdata-sections
UCXXDEFS = -fvisibility=hidden -fvisibility-inlines-hidden

# Browser simulator and host builds: stmlib's portable code paths.
UWASMDEFS = -DTEST -DM_PI=3.14159265358979323846 $(WARPS_DEFS)
