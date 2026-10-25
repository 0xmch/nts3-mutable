// Force-included in host builds ahead of the SDK's attributes.h (same include
// guard): Mach-O needs "segment,section" names, and optimize() is GCC-only.
#ifndef ATTRIBUTES_H_
#define ATTRIBUTES_H_
#define noopt
#define fast
#define force_inline inline __attribute__((always_inline))
#define fast_inline inline __attribute__((always_inline))
#define __unit_callback __attribute__((used))
#define __unit_header __attribute__((used))
#endif
