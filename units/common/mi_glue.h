// Helpers shared by the units: lookup tables built at init, and the 48 kHz <->
// 32 kHz resamplers for modules that run at 32 kHz (Clouds).

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mi_glue {

// sin(2 pi turns). Quarter-wave reduction, then Taylor to x^11 (error < 1e-7).
// Meant for filling tables at init, not for audio-rate use.
inline float SinTurns(float turns) {
  turns -= static_cast<float>(static_cast<int32_t>(turns));
  if (turns < 0.0f) turns += 1.0f;
  if (turns > 0.75f) {
    turns -= 1.0f;
  } else if (turns > 0.25f) {
    turns = 0.5f - turns;
  }
  float x = turns * 6.283185307f;
  float x2 = x * x;
  return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f *
      (1.0f - x2 / 72.0f * (1.0f - x2 / 110.0f)))));
}

// table[i] = sin(2 pi i / period) for i in [0, count).
inline void FillSine(float* table, size_t count, size_t period) {
  for (size_t i = 0; i < count; ++i) {
    size_t k = i % period;
    table[i] = SinTurns(static_cast<float>(k) / static_cast<float>(period));
  }
}

// Linear-phase lowpass at 96 kHz shared by both resamplers: Kaiser-windowed
// sinc, 128 taps, cutoff 15 kHz, beta 7.86 (about 80 dB stopband from 17 kHz).
const size_t kProtoTaps = 128;

inline float BesselI0(float x) {
  float sum = 1.0f, term = 1.0f, q = 0.25f * x * x;
  for (int k = 1; k < 32; ++k) {
    term *= q / static_cast<float>(k * k);
    sum += term;
    if (term < 1e-9f * sum) break;
  }
  return sum;
}

inline void DesignPrototype(float* h) {
  const float fc = 15000.0f / 96000.0f;
  const float beta = 7.86f;
  const float center = 0.5f * (kProtoTaps - 1);
  float i0_beta = BesselI0(beta), sum = 0.0f;
  for (size_t n = 0; n < kProtoTaps; ++n) {
    float t = static_cast<float>(n) - center;
    float sinc = SinTurns(fc * t) / (3.14159265f * t);  // t is never 0
    float r = t / center;
    float w = BesselI0(beta * __builtin_sqrtf(1.0f - r * r)) / i0_beta;
    h[n] = sinc * w;
    sum += h[n];
  }
  for (size_t n = 0; n < kProtoTaps; ++n) h[n] /= sum;
}

// Rational resampler, stereo, by L/M through the 96 kHz prototype. The history
// is stored twice so a filter pass always reads contiguous samples.
template <int L, int M>
class Resampler {
 public:
  static const size_t kTapsPerPhase = (kProtoTaps + L - 1) / L;

  void Init(const float* prototype) {
    for (int p = 0; p < L; ++p) {
      for (size_t i = 0; i < kTapsPerPhase; ++i) {
        size_t n = p + L * i;
        taps_[p][i] = n < kProtoTaps ? L * prototype[n] : 0.0f;
      }
    }
    for (size_t i = 0; i < 2 * kTapsPerPhase; ++i) {
      history_l_[i] = history_r_[i] = 0.0f;
    }
    head_ = 0;
  }

  inline void Push(float l, float r) {
    head_ = head_ == 0 ? kTapsPerPhase - 1 : head_ - 1;
    history_l_[head_] = history_l_[head_ + kTapsPerPhase] = l;
    history_r_[head_] = history_r_[head_ + kTapsPerPhase] = r;
  }

  // Output at `phase` 96 kHz ticks after the newest pushed sample.
  inline void Read(int phase, float* l, float* r) const {
    const float* t = taps_[phase];
    const float* hl = &history_l_[head_];
    const float* hr = &history_r_[head_];
    float sl = 0.0f, sr = 0.0f;
    for (size_t i = 0; i < kTapsPerPhase; ++i) {
      sl += t[i] * hl[i];
      sr += t[i] * hr[i];
    }
    *l = sl;
    *r = sr;
  }

 private:
  float taps_[L][kTapsPerPhase];
  float history_l_[2 * kTapsPerPhase];
  float history_r_[2 * kTapsPerPhase];
  size_t head_;
};

// 48 kHz -> 32 kHz: every 48 kHz frame advances the 96 kHz grid by 2 ticks,
// and an output is due every 3 ticks. phase_ is the distance from the newest
// input to the next output time; an output is computed once that distance is
// below 2, from inputs up to the newest only.
class Downsampler {
 public:
  void Init(const float* prototype) {
    resampler_.Init(prototype);
    phase_ = 2;
  }
  // Returns true when a 32 kHz frame was produced.
  inline bool Process(float l, float r, float* out_l, float* out_r) {
    resampler_.Push(l, r);
    phase_ -= 2;
    if (phase_ >= 2) return false;
    resampler_.Read(phase_, out_l, out_r);
    phase_ += 3;
    return true;
  }

 private:
  Resampler<2, 3> resampler_;
  int phase_;
};

// 32 kHz -> 48 kHz: every 48 kHz output advances the 96 kHz grid by 2 ticks,
// and a 32 kHz input is consumed every 3 ticks.
class Upsampler {
 public:
  void Init(const float* prototype) {
    resampler_.Init(prototype);
    phase_ = 1;
  }
  // How many 32 kHz frames the next Process() call will consume (0 or 1).
  inline int needed() const { return phase_ + 2 >= 3 ? 1 : 0; }
  inline void Process(float in_l, float in_r, float* out_l, float* out_r) {
    phase_ += 2;
    if (phase_ >= 3) {
      resampler_.Push(in_l, in_r);
      phase_ -= 3;
    }
    resampler_.Read(phase_, out_l, out_r);
  }

 private:
  Resampler<3, 2> resampler_;
  int phase_;
};

}  // namespace mi_glue
