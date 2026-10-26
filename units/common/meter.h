// Diagnostic builds only (scripts/meter.sh): with NTS3_METER defined, the unit
// times every render call with the Cortex-M7 cycle counter and reports over
// MIDI by stalling. A stalled render call holds up the firmware's MIDI replies
// by as long, which `nts3ctl watch` measures (probe unit: the core runs at
// 550 MHz, and a stall reads back within about a millisecond).
//
// Every 2 s the unit stalls for 16.5 M cycles (30 ms) plus a value. The first
// report gives the shortest period between render calls, at 64 cycles of
// stall per cycle. Later ones alternate between the slowest render call and
// the average one since the previous report, as a share of that period, at
// 275 k cycles (0.5 ms) per permille.
//
// What hangs the device is sustained overload: a single long call is fine
// (the probe stalled render for 285 ms). So a guard switches the unit to
// pass-through for good after 16 calls in a row over 60% of the period, or
// one call over 75% of it (not with NTS3_METER_SUSTAINED_ONLY, which measures
// a unit's occasional long calls), and the reports grow by another 550 M
// cycles (1 s), more than any value adds.
//
// meter_params.h, written by the script, defines NTS3_METER_PARAMS: {id, value}
// pairs set at init and kept, the firmware's own writes to them ignored.

#ifndef NTS3_MUTABLE_COMMON_METER_H_
#define NTS3_MUTABLE_COMMON_METER_H_

#ifdef NTS3_METER

#include <stdint.h>

#include "meter_params.h"

namespace meter {

inline uint32_t Cycles() { return *reinterpret_cast<volatile uint32_t*>(0xE0001004u); }

const uint32_t kReportInterval = 1100000000u;  // 2 s
const uint32_t kStallBase = 16500000u;
const uint32_t kStallTripped = 550000000u;
const uint32_t kStallPerPermille = 275000u;
const int16_t kParams[][2] = NTS3_METER_PARAMS;

const uint32_t kGuardCalls = 16;

// total: at most the 1.1 G cycles of a report interval, so 32 bits do.
uint32_t last_start, last_report, period, slowest, over, total;
uint32_t calls, reports;
bool tripped;
#ifdef NTS3_METER_PROFILE
}  // namespace meter
// Set by the instrumented code (see its NTS3_METER_PROFILE), reported after
// the slowest and average calls, then cleared.
uint32_t nts3_profile_max[2];
namespace meter {
#endif

template <class E>
void Init(E& effect) {
  for (const auto& p : kParams) effect.setParameter(p[0], p[1]);
  last_start = slowest = over = calls = reports = 0;
  total = 0;
  period = UINT32_MAX;
  tripped = false;
  last_report = Cycles();
}

inline bool Forced(uint8_t id) {
  for (const auto& p : kParams) {
    if (p[0] == id) return true;
  }
  return false;
}

template <class E>
void Render(E& effect, const float* in, float* out, uint32_t frames) {
  uint32_t start = Cycles();
  if (last_start && start - last_start < period) period = start - last_start;
  last_start = start;
  if (tripped) {
    for (uint32_t i = 0; i < 2 * frames; ++i) out[i] = in[i];
  } else {
    effect.process(in, out, frames);
  }
  uint32_t took = Cycles() - start;
  if (took > slowest) slowest = took;
  total += took;
  ++calls;
  over = period != UINT32_MAX && took > period / 10 * 6 ? over + 1 : 0;
#ifdef NTS3_METER_SUSTAINED_ONLY
  if (over >= kGuardCalls) tripped = true;
#else
  if (over >= kGuardCalls || (period != UINT32_MAX && took > period / 4 * 3)) tripped = true;
#endif

  if (start - last_report >= kReportInterval && period != UINT32_MAX) {
    uint32_t stall = kStallBase + (tripped ? kStallTripped : 0u);
    if (reports == 0) {
      stall += 64u * (period < 2000000u ? period : 2000000u);
    } else {
#ifdef NTS3_METER_PROFILE
      // In turn: slowest, average, profile 0, profile 1.
      const uint32_t k = (reports - 1) % 4;
      uint32_t cycles = k == 0 ? slowest : k == 1 ? total / calls : nts3_profile_max[k - 2];
      if (k >= 2) nts3_profile_max[k - 2] = 0;
#else
      uint32_t cycles = reports % 2 ? slowest : total / calls;
#endif
      uint32_t permille = cycles / (period / 1000u);
      stall += kStallPerPermille * (permille < 1500u ? permille : 1500u);
    }
    ++reports;
    slowest = calls = 0;
    total = 0;
    uint32_t begin = Cycles();
    while (Cycles() - begin < stall) {
    }
    last_start = 0;  // the stalled call's period does not count
    last_report = Cycles();
  }
}

}  // namespace meter

#endif  // NTS3_METER

#endif  // NTS3_MUTABLE_COMMON_METER_H_
