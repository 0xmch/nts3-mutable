// Host stand-in for CMSIS arm_math.h, which the logue SDK headers include via
// utils/cortexm.h. Only the intrinsics those headers use are provided.
#pragma once
#include <stdint.h>
static inline int32_t __SSAT(int32_t x, uint32_t bits) {
  int32_t max = (1 << (bits - 1)) - 1, min = -(1 << (bits - 1));
  return x > max ? max : (x < min ? min : x);
}
static inline uint32_t __USAT(int32_t x, uint32_t bits) {
  int32_t max = (1 << bits) - 1;
  return x > max ? max : (x < 0 ? 0 : x);
}
static inline uint32_t __CLZ(uint32_t x) { return x ? __builtin_clz(x) : 32; }

// SIMD helpers used by utils/fixed_math.h. __SEL picks bytes from a or b by
// the GE flags the previous __QSUB16 set, which the stubs keep in host_ge.
#define __SIMD32_TYPE int32_t
static uint32_t host_ge;
static inline int32_t __QSUB(int32_t a, int32_t b) {
  int64_t r = (int64_t)a - b;
  return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : (int32_t)r);
}
static inline int32_t __QSUB16(int32_t a, int32_t b) {
  int32_t lo = (int16_t)a - (int16_t)b, hi = (int16_t)(a >> 16) - (int16_t)(b >> 16);
  host_ge = (lo >= 0 ? 0x3u : 0u) | (hi >= 0 ? 0xCu : 0u);
  lo = lo > 32767 ? 32767 : (lo < -32768 ? -32768 : lo);
  hi = hi > 32767 ? 32767 : (hi < -32768 ? -32768 : hi);
  return (int32_t)(((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFF));
}
static inline int32_t __SEL(int32_t a, int32_t b) {
  uint32_t r = 0;
  for (int i = 0; i < 4; ++i) {
    uint32_t m = 0xFFu << (8 * i);
    r |= ((host_ge >> i) & 1 ? (uint32_t)a : (uint32_t)b) & m;
  }
  return (int32_t)r;
}
