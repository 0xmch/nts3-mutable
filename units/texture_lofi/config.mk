##############################################################################
# Texture Lo-Fi (granular, mono 8-bit): Mutable Instruments Clouds for the NTS-3 kaoss pad kit.
# Sources live in ../texture; this directory only picks the playback mode.
#

PROJECT := texture_lofi
PROJECT_TYPE := genericfx
TEXTURE_MODE := 0
TEXTURE_QUALITY := 3

MI := $(PROJECT_ROOT)/../../third_party/eurorack
TEXTURE := $(PROJECT_ROOT)/../texture
WASMMAIN := $(TEXTURE)/wasm.cc

UCSRC = $(TEXTURE)/header.c

UCXXSRC = $(TEXTURE)/unit.cc \
          $(MI)/clouds/dsp/granular_processor.cc \
          $(MI)/clouds/dsp/correlator.cc \
          $(MI)/clouds/dsp/mu_law.cc \
          $(TEXTURE)/no_spectral.cc \
          $(MI)/clouds/resources.cc \
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
UDEFS = -DM_PI=3.14159265358979323846 -DTEXTURE_MODE=$(TEXTURE_MODE) -DTEXTURE_QUALITY=$(TEXTURE_QUALITY)
UDEFS += -ffunction-sections -fdata-sections
UCXXDEFS = -fvisibility=hidden -fvisibility-inlines-hidden

# Browser simulator build: stmlib's portable code paths (no ARM assembly).
UWASMDEFS = -DTEST -DM_PI=3.14159265358979323846 -DTEXTURE_MODE=$(TEXTURE_MODE) -DTEXTURE_QUALITY=$(TEXTURE_QUALITY)
