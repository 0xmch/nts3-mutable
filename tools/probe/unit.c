// Probe: a pass-through unit for finding out what a unit can observe on the
// NTS-3, reporting by stalling a render call: the firmware's MIDI replies wait
// as long, which `nts3ctl listen` measures (core at 550 MHz, read back within
// a millisecond or so).
//
// This version counts the firmware's calls into the unit, to find out when it
// calls unit_reset() (the SDK asks for delay lines to be cleared there, which
// would wipe a granular buffer at the wrong moment): unit_init() again
// without a reload, unit_reset(), unit_suspend(), unit_resume(), touches
// began and ended, gaps of over 50 ms between render calls, and render calls
// between a suspend and the next resume. Every second it stalls for 50 ms
// plus 50 ms per count (capped at 10; the reply polling adds up to about
// 20 ms), one count after the other in that order, each cycle opened by a
// 700 ms marker. The counts are kept across
// unit_init() calls (a magic word in the unit's RAM tells a reload, after
// which they start over).
//
// Found (firmware 1.04, 2026-10-26): unit_reset() is not called when the pad
// is touched or released, when FX 1 is switched off and on, when another
// effect is selected and the unit again, or across a program change;
// selecting the unit loads it afresh (its RAM starts over), then calls
// unit_init() and unit_resume() once, and sends two touch "ended" events;
// while its module is off, the unit is not called at all.
//
// Earlier versions found: the firmware never reads unit_get_param_value back;
// units may read the DWT cycle counter; a stall in unit_render delays MIDI
// replies one for one, and stalls up to 285 ms are harmless; the SDRAM block
// starts at 0xC0000000; with only a pattern written at init and read after,
// nothing else changes the block in 40 s; plain stores of any pattern and
// memset() reach SDRAM intact, cleaned and invalidated from the D-cache.

#include <stdint.h>

#include "unit_genericfx.h"

#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004u)

enum { kInits, kResets, kSuspends, kResumes, kBegan, kEnded, kGaps, kSuspendedRenders, kCounts };

static const uint32_t kMagic = 0x50524F42u;          // 'PROB'
static const uint32_t kReportCycles = 550000000u;    // 1 s
static const uint32_t kGapCycles = 27500000u;        // 50 ms
static const uint32_t kMs = 550000u;

static uint32_t magic;
static uint32_t counts[kCounts];
static uint32_t last_report, last_end, next_field;
static uint8_t suspended, rendered;

static void Stall(uint32_t ms) {
  const uint32_t begin = DWT_CYCCNT;
  while (DWT_CYCCNT - begin < ms * kMs) {
  }
}

__unit_callback int8_t unit_init(const unit_runtime_desc_t* desc) {
  if (!desc) return k_unit_err_undef;
  if (desc->target != unit_header.common.target) return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;
  if (desc->input_channels != 2 || desc->output_channels != 2) return k_unit_err_geometry;
  if (magic != kMagic) {
    for (int i = 0; i < kCounts; ++i) counts[i] = 0;
    magic = kMagic;
  }
  ++counts[kInits];
  suspended = rendered = 0;
  next_field = 0;
  return k_unit_err_none;
}

__unit_callback void unit_teardown() {}

__unit_callback void unit_reset() { ++counts[kResets]; }

__unit_callback void unit_resume() {
  ++counts[kResumes];
  suspended = 0;
}

__unit_callback void unit_suspend() {
  ++counts[kSuspends];
  suspended = 1;
}

__unit_callback void unit_touch_event(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) {
  (void)id;
  (void)x;
  (void)y;
  if (phase == k_unit_touch_phase_began) ++counts[kBegan];
  if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled) ++counts[kEnded];
}

__unit_callback void unit_render(const float* in, float* out, uint32_t frames) {
  const uint32_t start = DWT_CYCCNT;
  for (uint32_t i = 0; i < 2 * frames; ++i) out[i] = in[i];
  if (suspended) ++counts[kSuspendedRenders];
  if (!rendered) {
    rendered = 1;
    last_report = start;
  } else if (start - last_end > kGapCycles) {
    ++counts[kGaps];
  }
  if (start - last_report >= kReportCycles) {
    if (next_field == 0) {
      Stall(700);
    } else {
      const uint32_t c = counts[next_field - 1];
      Stall(50 + 50 * (c < 10 ? c : 10));
    }
    next_field = (next_field + 1) % (kCounts + 1);
    last_report = DWT_CYCCNT;
  }
  last_end = DWT_CYCCNT;
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
  (void)id;
  (void)value;
}

__unit_callback int32_t unit_get_param_value(uint8_t id) {
  (void)id;
  return 0;
}

__unit_callback const char* unit_get_param_str_value(uint8_t id, int32_t value) {
  (void)id;
  (void)value;
  return 0;
}

__unit_callback void unit_set_tempo(uint32_t tempo) { (void)tempo; }
