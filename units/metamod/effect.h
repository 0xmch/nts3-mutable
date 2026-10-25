// Meta Mod and Freq Shifter: Mutable Instruments Warps as NTS-3 kaoss pad kit
// generic effects.
//
// The Warps DSP (third_party/eurorack, MIT, Emilie Gillet) runs unchanged at
// its native 96 kHz, between 48 -> 96 kHz and 96 -> 48 kHz resamplers, in
// 48-frame blocks (the vocoder decimates by 4 and 3, so blocks are multiples
// of 12). WARPS_SHIFTER picks the unit: 0 is Warps (cross-modulation and
// vocoder), 1 its easter egg, the frequency shifter.
//
// Warps has a carrier and a modulator input. With the internal oscillator as
// the carrier, the NTS-3 input (L+R) is the modulator. With Carrier set to
// "Input L", the left input is the carrier and the right input the modulator.
//
// Meta Mod pad: X = algorithm, Y = timbre, depth = dry/wet.
// Freq Shifter pad: X = shift (center = none), Y = feedback, depth = dry/wet.

#pragma once

#include <algorithm>
#include <cmath>
#include <new>

#include "processor.h"
#include "unit_genericfx.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"
#include "warps/dsp/modulator.h"
#include "warps/resources.h"

#include "../common/mi_glue.h"

#ifndef WARPS_SHIFTER
#define WARPS_SHIFTER 0
#endif

namespace metamod {
constexpr size_t Align(size_t size) { return (size + 7) & ~size_t(7); }
constexpr size_t kBlock = 48;              // frames at 96 kHz
constexpr size_t kBlockOut = kBlock / 2;   // frames at 48 kHz
constexpr size_t kTaps = 97;               // odd, so the pair delays 48 frames
constexpr size_t kFifoSize = 64;
constexpr size_t kFifoPrefill = kBlockOut;
// Dry path delay that lines Meta Mod's dry signal up with its wet signal
// through the cross-modulation path: the FIFO, both resamplers and Warps' own
// 6x oversampling (measured with host/harness).
constexpr size_t kDryDelay = 75;
constexpr size_t kDrySize = 128;
}  // namespace metamod

class Effect : public Processor {
 public:
#if WARPS_SHIFTER
  enum { SHIFT = 0U, FEEDBACK, MIX, SIDEBAND, CARRIER, NUM_PARAMS };
#else
  enum { ALGORITHM = 0U, TIMBRE, MIX, NOTE, CARRIER, MOD_DRIVE, CARRIER_DRIVE, NUM_PARAMS };
#endif

  static constexpr size_t kSdramBytes =
      metamod::Align(sizeof(warps::Modulator)) +
      metamod::Align(LUT_SIN_SIZE * sizeof(float)) +
      metamod::Align(2 * LUT_XFADE_IN_SIZE * sizeof(float)) +
      metamod::Align(LUT_BIPOLAR_FOLD_SIZE * sizeof(float)) +
      metamod::Align(6 * WAV_SINE_I_SIZE * sizeof(float));

  uint32_t getBufferSize() const override final {
    return (kSdramBytes + sizeof(float) - 1) / sizeof(float);
  }

  void init(float* allocated_buffer) override final {
    uint8_t* p = reinterpret_cast<uint8_t*>(allocated_buffer);
    modulator_ = new (p) warps::Modulator();
    p += metamod::Align(sizeof(warps::Modulator));
    p = BuildTables(p);

    modulator_->Init(96000.0f);
    modulator_->set_easter_egg(WARPS_SHIFTER);
    modulator_->set_bypass(false);
    warps::Parameters* w = modulator_->mutable_parameters();
    w->channel_drive[0] = w->channel_drive[1] = 0.0f;
    w->modulation_algorithm = w->modulation_parameter = 0.0f;
    w->frequency_shift_pot = 0.5f;
    w->frequency_shift_cv = 0.0f;
    w->phase_shift = 0.5f;
    w->note = 60.0f;
    w->carrier_shape = 1;

    float prototype[metamod::kTaps];
    mi_glue::DesignPrototype(prototype, metamod::kTaps, 21500.0f);
    up_.Init(prototype);
    down_.Init(prototype);

    for (int i = 0; i < NUM_PARAMS; ++i) {
      setParameter(i, unit_header.common.params[i].init);
    }
    for (int i = 0; i < 4; ++i) smoothed_[i] = target_[i];
    wet_ = target_wet_;
    reset();
  }

  void teardown() override final { modulator_ = nullptr; }

  void reset() override final {
    n_in_ = 0;
    fifo_read_ = 0;
    fifo_write_ = metamod::kFifoPrefill;
    std::fill(&fifo_l_[0], &fifo_l_[metamod::kFifoSize], 0.0f);
    std::fill(&fifo_r_[0], &fifo_r_[metamod::kFifoSize], 0.0f);
    std::fill(&dry_l_[0], &dry_l_[metamod::kDrySize], 0.0f);
    std::fill(&dry_r_[0], &dry_r_[metamod::kDrySize], 0.0f);
    dry_pos_ = 0;
  }

  void process(const float* __restrict in, float* __restrict out,
               uint32_t frames) override final {
    if (!modulator_) {
      std::copy(in, in + 2 * frames, out);
      return;
    }
    const bool internal = carrier_ != 0;
    for (uint32_t i = 0; i < frames; ++i, in += 2, out += 2) {
      // Warps' two inputs: carrier (or nothing, with the internal oscillator)
      // and modulator.
      float carrier = internal ? 0.0f : in[0];
      float modulator = internal ? 0.5f * (in[0] + in[1]) : in[1];
      float up[4];
      up_.Process(carrier, modulator, up);
      for (int k = 0; k < 2; ++k) {
        block_in_[n_in_].l = ToShort(up[2 * k]);
        block_in_[n_in_].r = ToShort(up[2 * k + 1]);
        ++n_in_;
      }
      if (n_in_ == metamod::kBlock) {
        n_in_ = 0;
        RenderBlock();
      }

      float wet_l = fifo_l_[fifo_read_];
      float wet_r = fifo_r_[fifo_read_];
      fifo_read_ = (fifo_read_ + 1) % metamod::kFifoSize;
#if WARPS_SHIFTER
      // The frequency shifter mixes dry and wet itself (channel_drive[1]).
      out[0] = wet_l;
      out[1] = wet_r;
#else
      // Warps has no dry/wet: mix with the input, delayed to line up.
      dry_l_[dry_pos_] = in[0];
      dry_r_[dry_pos_] = in[1];
      size_t tap = (dry_pos_ + metamod::kDrySize - metamod::kDryDelay) % metamod::kDrySize;
      dry_pos_ = (dry_pos_ + 1) % metamod::kDrySize;
      wet_ += 0.0005f * (target_wet_ - wet_);
      float dry = std::min(1.0f, 2.0f * (1.0f - wet_));
      float wet = std::min(1.0f, 2.0f * wet_);
      out[0] = dry * dry_l_[tap] + wet * wet_l;
      out[1] = dry * dry_r_[tap] + wet * wet_l;
#endif
    }
  }

  void setParameter(uint8_t index, int32_t value) override final {
    switch (index) {
#if WARPS_SHIFTER
      case SHIFT: target_[0] = value * 0.001f; break;
      case FEEDBACK: target_[1] = value * 0.001f; break;
      case MIX: target_[2] = (value + 1000) * 0.0005f; break;
      case SIDEBAND: target_[3] = value * 0.05f; break;
      case CARRIER: carrier_ = value < 3 ? value + 1 : 0; break;
#else
      case ALGORITHM: {
        // Warps' fix for wavefolder bleed near the bottom of the knob.
        float a = value * 0.001f;
        if (a <= 0.125f) a = std::min(1.0f, std::max(0.0f, a * 1.08f - 0.01f));
        target_[0] = a;
        break;
      }
      case TIMBRE: target_[1] = value * 0.001f; break;
      case MIX: target_wet_ = (value + 1000) * 0.0005f; break;
      case NOTE: note_ = static_cast<float>(value); break;
      case CARRIER: carrier_ = value; break;
      // With nothing patched in their CV inputs, Warps' level knobs set the
      // drive to the square of their position.
      case MOD_DRIVE: target_[2] = value * value * 1e-6f; break;
      case CARRIER_DRIVE: target_[3] = value * value * 1e-6f; break;
#endif
      default: break;
    }
  }

  const char* getParameterStrValue(uint8_t index,
                                   int32_t value) const override final {
    static char text[16];
    switch (index) {
#if WARPS_SHIFTER
      case SHIFT: {
        float hz = ShiftHz(value * 0.001f);
        float a = std::fabs(hz);
        const char* sign = hz > 0.0f ? "+" : hz < 0.0f ? "-" : "";
        if (a >= 1000.0f) Format(text, sign, a / 1000.0f, 2, " kHz");
        else if (a >= 100.0f) Format(text, sign, a, 0, " Hz");
        else if (a >= 10.0f) Format(text, sign, a, 1, " Hz");
        else Format(text, sign, a, 2, " Hz");
        return text;
      }
      case SIDEBAND:
        if (value == 0) return "Down";
        if (value == 20) return "Up";
        if (value == 10) return "Both";
        Format(text, value < 10 ? "Down " : "Up ", (value < 10 ? 20 - value : value) * 5.0f, 0, "%");
        return text;
      case CARRIER: {
        static const char* const carriers[] = {"Sine", "Harmonics", "Buzzy", "Input L"};
        return value >= 0 && value < 4 ? carriers[value] : nullptr;
      }
#else
      case CARRIER: {
        static const char* const carriers[] = {"Input L", "Sine", "Triangle", "Saw"};
        return value >= 0 && value < 4 ? carriers[value] : nullptr;
      }
#endif
      default:
        break;
    }
    return nullptr;
  }

 private:
  static inline short ToShort(float x) {
    int32_t s = static_cast<int32_t>(x * 32768.0f);
    return static_cast<short>(s < -32768 ? -32768 : (s > 32767 ? 32767 : s));
  }

  // Minimal "%s%.*f%s" (no printf in the unit).
  static void Format(char* out, const char* prefix, float v, int decimals,
                     const char* suffix) {
    while (*prefix) *out++ = *prefix++;
    int32_t scale = decimals == 2 ? 100 : decimals == 1 ? 10 : 1;
    int32_t n = static_cast<int32_t>(v * scale + 0.5f);
    char digits[12];
    int count = 0;
    do {
      digits[count++] = static_cast<char>('0' + n % 10);
      n /= 10;
    } while (n || count <= decimals);
    while (count) {
      if (count == decimals) *out++ = '.';
      *out++ = digits[--count];
    }
    while (*suffix) *out++ = *suffix++;
    *out = '\0';
  }

#if WARPS_SHIFTER
  // Shift frequency in Hz for a pot position, as in
  // Modulator::ProcessEasterEgg with no CV.
  static float ShiftHz(float pot) {
    float direction = pot >= 0.5f ? 1.0f : -1.0f;
    float x = 2.0f * std::fabs(pot - 0.5f);
    float hz = x <= 0.4f ? x * x * x * 62.5f
                         : 4.0f * stmlib::SemitonesToRatio(180.0f * (x - 0.4f));
    return x == 0.0f ? 0.0f : direction * hz;
  }
#endif

  // Warps' resources.py and waveforms.py, at init. lut_sin is sin(2 pi i /
  // 1024) over 1.25 periods; lut_xfade_in/out are sin/cos(pi/2 t'), with
  // t' = 1.04 t - 0.02 clamped to [0, 1], over sqrt(2); the wav_* tables are
  // the I (as is, DC removed) and Q (90 degrees ahead) parts of three
  // waveforms, scaled together to a peak of 1.
  uint8_t* BuildTables(uint8_t* p) {
    float* sine = reinterpret_cast<float*>(p);
    mi_glue::FillSine(sine, LUT_SIN_SIZE, 1024);
    warps::lut_sin = sine;
    p += metamod::Align(LUT_SIN_SIZE * sizeof(float));

    float* xfade = reinterpret_cast<float*>(p);
    for (size_t i = 0; i < LUT_XFADE_IN_SIZE; ++i) {
      float t = 1.04f * static_cast<float>(i) / LUT_XFADE_IN_SIZE - 0.02f;
      t = std::min(1.0f, std::max(0.0f, t));
      xfade[i] = mi_glue::SinTurns(0.25f * t) * 0.70710678f;
      xfade[LUT_XFADE_IN_SIZE + i] = mi_glue::SinTurns(0.25f * t + 0.25f) * 0.70710678f;
    }
    warps::lut_xfade_in = xfade;
    warps::lut_xfade_out = xfade + LUT_XFADE_IN_SIZE;
    p += metamod::Align(2 * LUT_XFADE_IN_SIZE * sizeof(float));

    float* fold = reinterpret_cast<float*>(p);
    for (size_t i = 0; i < LUT_BIPOLAR_FOLD_SIZE; ++i) {
      fold[i] = warps::lut_bipolar_fold_q15[i] * warps::kBipolarFoldScale;
    }
    warps::lut_bipolar_fold = fold;
    p += metamod::Align(LUT_BIPOLAR_FOLD_SIZE * sizeof(float));

    // Harmonic series of the three waveforms: amplitude and phase per harmonic.
    float* wav = reinterpret_cast<float*>(p);
    const size_t n = WAV_SINE_I_SIZE;
    for (int w = 0; w < 3; ++w) {
      float* table_i = wav + (2 * w) * n;
      float* table_q = wav + (2 * w + 1) * n;
      float peak = 0.0f;
      for (size_t k = 0; k < n; ++k) {
        float t = static_cast<float>(k) / (n - 1);  // turns
        float i_value = 0.0f, q_value = 0.0f;
        for (int h = 1; h <= 7; ++h) {
          float amplitude = 0.0f, phase = 0.0f;  // phase in turns
          if (w == 0) {
            amplitude = h == 1 ? -1.0f : 0.0f;
          } else if (w == 1) {
            amplitude = h == 1 ? -1.0f : h == 2 ? -0.5f : h == 5 ? 0.5f : 0.0f;
          } else {
            amplitude = mi_glue::SinTurns(1.123f * (h - 1) / 6.2831853f);
            phase = 1.012f * (h - 1) / 6.2831853f;
          }
          if (amplitude == 0.0f) continue;
          i_value += amplitude * mi_glue::SinTurns(h * t + phase);
          q_value += amplitude * mi_glue::SinTurns(h * t + phase + 0.25f);
        }
        table_i[k] = i_value;
        table_q[k] = q_value;
        peak = std::max(peak, std::max(std::fabs(i_value), std::fabs(q_value)));
      }
      for (size_t k = 0; k < n; ++k) {
        table_i[k] /= peak;
        table_q[k] /= peak;
      }
      warps::wav_table[2 * w] = table_i;
      warps::wav_table[2 * w + 1] = table_q;
    }
    p += metamod::Align(6 * n * sizeof(float));
    return p;
  }

  void RenderBlock() {
    // Knob smoothing per block, as in Warps' cv_scaler.cc (BIND).
    for (int i = 0; i < 4; ++i) smoothed_[i] += 0.026f * (target_[i] - smoothed_[i]);
    warps::Parameters* w = modulator_->mutable_parameters();
    w->carrier_shape = carrier_;
#if WARPS_SHIFTER
    w->frequency_shift_pot = smoothed_[0];
    w->frequency_shift_cv = 0.0f;
    w->phase_shift = smoothed_[0];
    w->channel_drive[0] = smoothed_[1];
    w->channel_drive[1] = smoothed_[2];
    w->modulation_parameter = smoothed_[3];
    w->note = 60.0f;
#else
    w->modulation_algorithm = smoothed_[0];
    w->modulation_parameter = smoothed_[1];
    w->channel_drive[1] = smoothed_[2];
    w->channel_drive[0] = smoothed_[3];
    note_smoothed_ += 0.1f * (note_ - note_smoothed_);
    w->note = note_smoothed_;
#endif
    modulator_->Process(block_in_, block_out_, metamod::kBlock);

    for (size_t i = 0; i < metamod::kBlock; ++i) {
      float l, r;
#if WARPS_SHIFTER
      float main = block_out_[i].l / 32768.0f;
      float aux = block_out_[i].r / 32768.0f;
#else
      float main = block_out_[i].l / 32768.0f;
      float aux = main;
#endif
      if (down_.Process(main, aux, &l, &r)) {
        fifo_l_[fifo_write_] = l;
        fifo_r_[fifo_write_] = r;
        fifo_write_ = (fifo_write_ + 1) % metamod::kFifoSize;
      }
    }
  }

  warps::Modulator* modulator_ = nullptr;
  mi_glue::Upsampler2<metamod::kTaps> up_;
  mi_glue::Downsampler2<metamod::kTaps> down_;

  warps::ShortFrame block_in_[metamod::kBlock];
  warps::ShortFrame block_out_[metamod::kBlock];
  size_t n_in_ = 0;
  float fifo_l_[metamod::kFifoSize];
  float fifo_r_[metamod::kFifoSize];
  size_t fifo_read_ = 0;
  size_t fifo_write_ = 0;
  float dry_l_[metamod::kDrySize];
  float dry_r_[metamod::kDrySize];
  size_t dry_pos_ = 0;

  // Written by the UI callbacks, read once per block. Meta Mod: algorithm,
  // timbre, modulator drive, carrier drive. Freq Shifter: shift, feedback,
  // dry/wet, sideband.
  float target_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float smoothed_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float target_wet_ = 1.0f;
  float wet_ = 1.0f;
  float note_ = 60.0f;
  float note_smoothed_ = 60.0f;
  int32_t carrier_ = 1;
};
