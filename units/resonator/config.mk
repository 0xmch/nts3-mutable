##############################################################################
# Resonator: Mutable Instruments Rings for the NTS-3 kaoss pad kit
#

PROJECT := resonator
PROJECT_TYPE := genericfx

MI := $(PROJECT_ROOT)/../../third_party/eurorack

UCSRC = header.c

UCXXSRC = unit.cc \
          $(MI)/rings/dsp/part.cc \
          $(MI)/rings/dsp/resonator.cc \
          $(MI)/rings/dsp/string.cc \
          $(MI)/rings/dsp/fm_voice.cc \
          $(MI)/rings/resources.cc \
          $(MI)/stmlib/dsp/units.cc \
          $(MI)/stmlib/utils/random.cc

UASMSRC =

UASMXSRC =

UINCDIR = $(MI)

ULIBDIR =

ULIBS = -lm -Wl,--gc-sections

# stmlib uses M_PI, which strict -std=c++11 hides in newlib.
UDEFS = -DM_PI=3.14159265358979323846
# The unit image is capped at about 32 KB: section GC, and C++ symbols stay
# out of the ELF dynamic symbol table (unit.cc exports the unit API). C files
# keep default visibility: _unit_base.c's weak fallbacks would otherwise hide
# the real unit_* symbols at link time.
UDEFS += -ffunction-sections -fdata-sections
UCXXDEFS = -fvisibility=hidden -fvisibility-inlines-hidden
