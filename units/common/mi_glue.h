// Helpers shared by the units: lookup tables built at init, and resamplers for
// modules that run at their own rate: 48 <-> 32 kHz (Clouds) and 48 <-> 96 kHz
// (Warps).

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

// Linear-phase lowpass prototypes at 96 kHz: Kaiser-windowed sinc, beta 7.86
// (about 80 dB stopband). The 48/32 kHz pair uses 128 taps cut at 15 kHz
// (stopband from 17 kHz).
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

inline void DesignPrototype(float* h, size_t taps = kProtoTaps,
                            float cutoff_hz = 15000.0f) {
  const float fc = cutoff_hz / 96000.0f;
  const float beta = 7.86f;
  const float center = 0.5f * (taps - 1);
  float i0_beta = BesselI0(beta), sum = 0.0f;
  for (size_t n = 0; n < taps; ++n) {
    float t = static_cast<float>(n) - center;
    float sinc = t == 0.0f ? 2.0f * fc : SinTurns(fc * t) / (3.14159265f * t);
    float r = t / center;
    float w = BesselI0(beta * __builtin_sqrtf(1.0f - r * r)) / i0_beta;
    h[n] = sinc * w;
    sum += h[n];
  }
  for (size_t n = 0; n < taps; ++n) h[n] /= sum;
}

// Rational resampler, stereo, by L/M through a 96 kHz prototype of TAPS taps.
// The history is stored twice so a filter pass always reads contiguous
// samples.
template <int L, int M, size_t TAPS = kProtoTaps>
class Resampler {
 public:
  static const size_t kTapsPerPhase = (TAPS + L - 1) / L;

  void Init(const float* prototype) {
    for (int p = 0; p < L; ++p) {
      for (size_t i = 0; i < kTapsPerPhase; ++i) {
        size_t n = p + L * i;
        taps_[p][i] = n < TAPS ? L * prototype[n] : 0.0f;
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

// 48 kHz -> 96 kHz: two outputs per input, at 0 and 1 tick after it.
template <size_t TAPS>
class Upsampler2 {
 public:
  void Init(const float* prototype) { resampler_.Init(prototype); }
  // out: l0, r0, l1, r1.
  inline void Process(float l, float r, float* out) {
    resampler_.Push(l, r);
    resampler_.Read(0, &out[0], &out[1]);
    resampler_.Read(1, &out[2], &out[3]);
  }

 private:
  Resampler<2, 1, TAPS> resampler_;
};

// 96 kHz -> 48 kHz: lowpass at 96 kHz, keep every second sample.
template <size_t TAPS>
class Downsampler2 {
 public:
  void Init(const float* prototype) {
    resampler_.Init(prototype);
    odd_ = false;
  }
  // Returns true when a 48 kHz frame was produced.
  inline bool Process(float l, float r, float* out_l, float* out_r) {
    resampler_.Push(l, r);
    odd_ = !odd_;
    if (odd_) return false;
    resampler_.Read(0, out_l, out_r);
    return true;
  }

 private:
  Resampler<1, 2, TAPS> resampler_;
  bool odd_;
};

}  // namespace mi_glue
