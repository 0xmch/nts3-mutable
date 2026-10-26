##############################################################################
# Probe: pass-through diagnostic unit (see unit.c)
#

PROJECT := probe
PROJECT_TYPE := genericfx

UCSRC = header.c unit.c
UCXXSRC =
UASMSRC =
UASMXSRC =
UINCDIR =
ULIBDIR =
ULIBS = -Wl,--gc-sections
UDEFS = -ffunction-sections -fdata-sections
