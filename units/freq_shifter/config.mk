##############################################################################
# Freq Shifter: the Warps easter egg, for the NTS-3 kaoss pad kit.
# Sources live in ../metamod; this directory only picks the unit.
#

PROJECT := freq_shifter
PROJECT_TYPE := genericfx
WARPS_SHIFTER := 1

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

# stmlib uses M_PI, which strict -std=c++11 hides in newlib.
UDEFS = -DM_PI=3.14159265358979323846 -DWARPS_SHIFTER=$(WARPS_SHIFTER)
UDEFS += -ffunction-sections -fdata-sections
UCXXDEFS = -fvisibility=hidden -fvisibility-inlines-hidden

# Browser simulator and host builds: stmlib's portable code paths.
UWASMDEFS = -DTEST -DM_PI=3.14159265358979323846 -DWARPS_SHIFTER=$(WARPS_SHIFTER)
