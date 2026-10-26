// Meta Mod and Freq Shifter: Mutable Instruments Warps as NTS-3 kaoss pad kit
// generic effects.
//
// The Warps DSP (third_party/eurorack, MIT, Emilie Gillet) runs in 0.5 ms
// blocks (the vocoder decimates by 4 and 3, so blocks are multiples of 12),
// at WARPS_SAMPLE_RATE (warps/dsp/rate.h). The frequency shifter runs at
// Warps' native 96 kHz, between 48 -> 96 kHz and 96 -> 48 kHz resamplers.
// Meta Mod runs at the NTS-3's 48 kHz: at 96 kHz it took 50-60% of the
// device's render time in cross-modulation and hung the device in the
// vocoder. WARPS_SHIFTER picks the unit: 0 is Warps (cross-modulation and
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
#include "stmlib/utils/buffer_allocator.h"
#include "warps/dsp/modulator.h"
#include "warps/dsp/rate.h"
#include "warps/resources.h"

#include "../common/mi_glue.h"

#ifndef WARPS_SHIFTER
#define WARPS_SHIFTER 0
#endif

namespace metamod {
constexpr size_t Align(size_t size) { return (size + 7) & ~size_t(7); }
constexpr size_t kRatio = WARPS_SAMPLE_RATE / 48000;  // Warps' rate / the NTS-3's
constexpr size_t kBlock = 24 * kRatio;      // frames at Warps' rate
constexpr size_t kBlockOut = kBlock / kRatio;  // frames at 48 kHz
constexpr size_t kTaps = 97;                // odd, so the pair delays 48 frames
constexpr size_t kFifoSize = 64;
constexpr size_t kFifoPrefill = kBlockOut;
// Dry path delay that lines Meta Mod's dry signal up with its wet signal
// through the cross-modulation path: the FIFO and Warps' own 6x oversampling
// (measured with host/harness).
constexpr size_t kDryDelay = 41;
constexpr size_t kDrySize = 128;

// Working buffers, plain samples only: they live in SDRAM with Warps' own
// buffers (the unit image, code and RAM, is capped at 32 KB).
struct State {
#if WARPS_SAMPLE_RATE == 96000
  // The resamplers' taps and history (Effect::up_, down_).
  float up_storage[mi_glue::Upsampler2<kTaps>::kStorageSize];
  float down_storage[mi_glue::Downsampler2<kTaps>::kStorageSize];
  // Only used at init, but unit callbacks run on the firmware's stack, which
  // has no room for it.
  float prototype[kTaps];
#endif
  warps::ShortFrame block_in[kBlock];
  warps::ShortFrame block_out[kBlock];
  float fifo_l[kFifoSize];
  float fifo_r[kFifoSize];
  float dry_l[kDrySize];
  float dry_r[kDrySize];
};

// Warps' state, in the unit's own RAM (see Effect::kSdramBytes). Outside the
// Effect object, which keeps a constant initializer: nothing needs a static
// constructor at load.
alignas(warps::Modulator) static uint8_t modulator_storage[sizeof(warps::Modulator)];
}  // namespace metamod

class Effect : public Processor {
 public:
#if WARPS_SHIFTER
  enum { SHIFT = 0U, FEEDBACK, MIX, SIDEBAND, CARRIER, NUM_PARAMS };
#else
  enum { ALGORITHM = 0U, TIMBRE, MIX, NOTE, CARRIER, MOD_DRIVE, CARRIER_DRIVE, NUM_PARAMS };
#endif

  // SDRAM: Warps' buffers, the working buffers and the tables. The rest of
  // Warps' state lives in metamod::modulator_storage, in the unit's own RAM:
  // on the NTS-3, parts of state written to SDRAM did not read back (part of
  // a word, now and then), and every published NTS-3 unit, Korg's included,
  // keeps its DSP objects in static RAM and only plain sample buffers in
  // SDRAM.
  static constexpr size_t kWarpsBytes = warps::kModulatorBufferSize * sizeof(float);
  static constexpr size_t kSdramBytes =
      metamod::Align(kWarpsBytes) +
      metamod::Align(sizeof(metamod::State)) +
      metamod::Align(LUT_SIN_SIZE * sizeof(float)) +
      metamod::Align((LUT_MIDI_TO_F_HIGH_SIZE + LUT_MIDI_TO_F_LOW_SIZE) * sizeof(float)) +
      metamod::Align(2 * LUT_XFADE_IN_SIZE * sizeof(float)) +
      metamod::Align(LUT_BIPOLAR_FOLD_SIZE * sizeof(float)) +
      metamod::Align(6 * WAV_SINE_I_SIZE * sizeof(float));

  uint32_t getBufferSize() const override final {
    return (kSdramBytes + sizeof(float) - 1) / sizeof(float);
  }

  // The firmware's raw input (unit_runtime_genericfx_context_t), or null.
  void setRawInput(unit_runtime_genericfx_get_raw_input_ptr get_raw_input) {
    get_raw_input_ = get_raw_input;
  }

  void init(float* allocated_buffer) override final {
    // Warps relies on its state starting at zero (as in the module's RAM);
    // the unit's static RAM is not cleared when it loads.
    uint8_t* storage = metamod::modulator_storage;
    std::fill(storage, storage + sizeof(metamod::modulator_storage), 0);
    modulator_ = new (storage) warps::Modulator();
    uint8_t* p = reinterpret_cast<uint8_t*>(allocated_buffer);
    stmlib::BufferAllocator allocator(p, kWarpsBytes);
    p += metamod::Align(kWarpsBytes);
    s_ = new (p) metamod::State();
    p += metamod::Align(sizeof(metamod::State));
    p = BuildTables(p);

    modulator_->Init(static_cast<float>(WARPS_SAMPLE_RATE), &allocator);
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

#if WARPS_SAMPLE_RATE == 96000
    mi_glue::DesignPrototype(s_->prototype, metamod::kTaps, 21500.0f);
    up_.Init(s_->prototype, s_->up_storage);
    down_.Init(s_->prototype, s_->down_storage);
#endif

    for (int i = 0; i < NUM_PARAMS; ++i) {
      setParameter(i, unit_header.common.params[i].init);
    }
    for (int i = 0; i < 4; ++i) smoothed_[i] = target_[i];
    wet_ = target_wet_;
    note_smoothed_ = note_;
    touching_ = false;
    touch_gain_ = 0.0f;
    reset();
  }

  void teardown() override final {
    modulator_ = nullptr;
    s_ = nullptr;
  }

  void reset() override final {
    if (!s_) return;
    n_in_ = 0;
    fifo_read_ = 0;
    fifo_write_ = metamod::kFifoPrefill;
    std::fill(&s_->fifo_l[0], &s_->fifo_l[metamod::kFifoSize], 0.0f);
    std::fill(&s_->fifo_r[0], &s_->fifo_r[metamod::kFifoSize], 0.0f);
    std::fill(&s_->dry_l[0], &s_->dry_l[metamod::kDrySize], 0.0f);
    std::fill(&s_->dry_r[0], &s_->dry_r[metamod::kDrySize], 0.0f);
    dry_pos_ = 0;
  }

  void process(const float* __restrict in, float* __restrict out,
               uint32_t frames) override final {
    if (!modulator_) {
      std::copy(in, in + 2 * frames, out);
      return;
    }
    const bool internal = carrier_ != 0;
#if !WARPS_SHIFTER
    // The NTS-3 always adds the effect's output and crossfades its input to
    // silence when the effect is off; on is the pad touched, or the input
    // routed to the effect (within 60 dB of the raw input: Hold, release).
    const float* raw = get_raw_input_ ? get_raw_input_() : nullptr;
    if (raw) {
      float routed = 0.0f, unrouted = 0.0f;
      for (uint32_t i = 0; i < 2 * frames; ++i) {
        routed += in[i] * in[i];
        unrouted += raw[i] * raw[i];
      }
      if (unrouted > 1e-12f) routed_on_ = routed > 1e-6f * unrouted;
    }
    const float touch_target = touching_ || (raw && routed_on_) ? 1.0f : 0.0f;
#endif
    for (uint32_t i = 0; i < frames; ++i, in += 2, out += 2) {
      // Warps' two inputs: carrier (or nothing, with the internal oscillator)
      // and modulator.
      float carrier = internal ? 0.0f : in[0];
      float modulator = internal ? 0.5f * (in[0] + in[1]) : in[1];
#if WARPS_SAMPLE_RATE == 96000
      float up[4];
      up_.Process(carrier, modulator, up);
      for (int k = 0; k < 2; ++k) {
        s_->block_in[n_in_].l = ToShort(up[2 * k]);
        s_->block_in[n_in_].r = ToShort(up[2 * k + 1]);
        ++n_in_;
      }
#else
      s_->block_in[n_in_].l = ToShort(carrier);
      s_->block_in[n_in_].r = ToShort(modulator);
      ++n_in_;
#endif
      if (n_in_ == metamod::kBlock) {
        n_in_ = 0;
        RenderBlock();
      }

      float wet_l = s_->fifo_l[fifo_read_];
      float wet_r = s_->fifo_r[fifo_read_];
      fifo_read_ = (fifo_read_ + 1) % metamod::kFifoSize;
#if WARPS_SHIFTER
      // The frequency shifter mixes dry and wet itself (channel_drive[1]).
      out[0] = wet_l;
      out[1] = wet_r;
#else
      // Warps has no dry/wet: mix with the input, delayed to line up.
      s_->dry_l[dry_pos_] = in[0];
      s_->dry_r[dry_pos_] = in[1];
      size_t tap = (dry_pos_ + metamod::kDrySize - metamod::kDryDelay) % metamod::kDrySize;
      dry_pos_ = (dry_pos_ + 1) % metamod::kDrySize;
      wet_ += 0.0005f * (target_wet_ - wet_);
      float dry = std::min(1.0f, 2.0f * (1.0f - wet_));
      float wet = std::min(1.0f, 2.0f * wet_);
      // The effect follows the pad, as on a Kaoss pad: off, the wet signal
      // fades out (about 50 ms) and the input passes dry. The NTS-3 adds the
      // effect's output even when it is off, and Warps' internal oscillator
      // never stops (a tone hung on after release).
      touch_gain_ += 0.002f * (touch_target - touch_gain_);
      if (std::fabs(touch_target - touch_gain_) < 1e-4f) touch_gain_ = touch_target;
      dry += (1.0f - dry) * (1.0f - touch_gain_);
      wet *= touch_gain_;
      out[0] = dry * s_->dry_l[tap] + wet * wet_l;
      out[1] = dry * s_->dry_r[tap] + wet * wet_r;
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

#if !WARPS_SHIFTER
  void touchEvent(uint8_t id, uint8_t phase, uint32_t x,
                  uint32_t y) override final {
    (void)id;
    (void)x;
    (void)y;
    switch (phase) {
      case k_unit_touch_phase_began:
        touching_ = true;
        break;
      case k_unit_touch_phase_ended:
      case k_unit_touch_phase_cancelled:
        touching_ = false;
        break;
      default:
        break;
    }
  }

#endif
  const char* getParameterStrValue(uint8_t index,
                                   int32_t value) const override final {
#if WARPS_SHIFTER
    static char text[16];
#endif
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

    // Wavefolder: Q15 values from second differences (see resources.h).
    float* fold = reinterpret_cast<float*>(p);
    const int8_t* d2 = warps::lut_bipolar_fold_d2;
    int32_t q = warps::kBipolarFoldStart[0];
    int32_t d = warps::kBipolarFoldStart[1];
    fold[0] = q * warps::kBipolarFoldScale;
    for (size_t i = 1; i < LUT_BIPOLAR_FOLD_SIZE; ++i) {
      if (i > 1) {
        int32_t step = *d2++;
        if (step == -128) {
          step = static_cast<int16_t>(static_cast<uint8_t>(d2[0]) | (static_cast<uint8_t>(d2[1]) << 8));
          d2 += 2;
        }
        d += step;
      }
      q += d;
      fold[i] = q * warps::kBipolarFoldScale;
    }
    warps::lut_bipolar_fold = fold;
    p += metamod::Align(LUT_BIPOLAR_FOLD_SIZE * sizeof(float));

    // MIDI note to frequency at Warps' rate, capped at 12 kHz: note (i - 48)
    // in the high table, 1/256 semitone steps in the low one.
    float* midi = reinterpret_cast<float*>(p);
    for (size_t i = 0; i < LUT_MIDI_TO_F_HIGH_SIZE; ++i) {
      // stmlib's ratio table covers +-127 semitones; above that it is capped.
      float semitones = std::min(127.0f, static_cast<float>(i) - 48.0f - 69.0f);
      float f = 440.0f * stmlib::SemitonesToRatio(semitones);
      midi[i] = std::min(f, 12000.0f) / static_cast<float>(WARPS_SAMPLE_RATE);
    }
    for (size_t i = 0; i < LUT_MIDI_TO_F_LOW_SIZE; ++i) {
      midi[LUT_MIDI_TO_F_HIGH_SIZE + i] = stmlib::SemitonesToRatio(static_cast<float>(i) / 256.0f);
    }
    warps::lut_midi_to_f_high = midi;
    warps::lut_midi_to_f_low = midi + LUT_MIDI_TO_F_HIGH_SIZE;
    p += metamod::Align((LUT_MIDI_TO_F_HIGH_SIZE + LUT_MIDI_TO_F_LOW_SIZE) * sizeof(float));

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
#if WARPS_SHIFTER
    // Straight to the easter egg: Process() would pull the whole
    // cross-modulation and vocoder engine into the unit.
    modulator_->ProcessEasterEgg(s_->block_in, s_->block_out, metamod::kBlock);
#else
    modulator_->Process(s_->block_in, s_->block_out, metamod::kBlock);
#endif

    for (size_t i = 0; i < metamod::kBlock; ++i) {
      float l, r;
#if WARPS_SHIFTER
      float main = s_->block_out[i].l / 32768.0f;
      float aux = s_->block_out[i].r / 32768.0f;
#else
      float main = s_->block_out[i].l / 32768.0f;
      float aux = main;
#endif
#if WARPS_SAMPLE_RATE == 96000
      if (!down_.Process(main, aux, &l, &r)) continue;
#else
      l = main;
      r = aux;
#endif
      s_->fifo_l[fifo_write_] = l;
      s_->fifo_r[fifo_write_] = r;
      fifo_write_ = (fifo_write_ + 1) % metamod::kFifoSize;
    }
  }

  warps::Modulator* modulator_ = nullptr;
  metamod::State* s_ = nullptr;
#if WARPS_SAMPLE_RATE == 96000
  mi_glue::Upsampler2<metamod::kTaps> up_;
  mi_glue::Downsampler2<metamod::kTaps> down_;
#endif
  size_t n_in_ = 0;
  size_t fifo_read_ = 0;
  size_t fifo_write_ = 0;
  size_t dry_pos_ = 0;

  // Written by the UI callbacks, read once per block. Meta Mod: algorithm,
  // timbre, modulator drive, carrier drive. Freq Shifter: shift, feedback,
  // dry/wet, sideband.
  float target_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float smoothed_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float target_wet_ = 1.0f;
  float wet_ = 1.0f;
  volatile bool touching_ = false;  // Meta Mod: the pad gates the effect
  unit_runtime_genericfx_get_raw_input_ptr get_raw_input_ = nullptr;
  bool routed_on_ = false;  // the firmware routes the input to the effect
  float touch_gain_ = 0.0f;
  float note_ = 60.0f;
  float note_smoothed_ = 60.0f;
  int32_t carrier_ = 1;
};
