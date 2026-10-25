// Resonator: Mutable Instruments Rings as an NTS-3 kaoss pad kit generic effect.
//
// The Rings DSP (third_party/eurorack, MIT, Emilie Gillet) runs unchanged; this
// file plays the role of the Rings front panel and CV scaler.
//
// Pad: X = note, Y = structure, depth = dry/wet.
// Touching the pad strums a new voice, and so does sliding X to a new semitone
// while touching. With audio on the input, the input excites the resonator and
// its transients strum too (Rings with IN patched). With a silent input the
// internal exciter takes over (Rings with nothing patched), so tapping the pad
// plucks the resonator.

#pragma once

#include <algorithm>
#include <cmath>
#include <new>

#include "processor.h"
#include "unit_genericfx.h"

#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"
#include "rings/resources.h"

namespace resonator {
// Rounds SDRAM carve-outs up to 8 bytes.
constexpr size_t Align(size_t size) { return (size + 7) & ~size_t(7); }
// Same as rings::kSampleRate, which is not constexpr.
constexpr float kSampleRate = 48000.0f;
}  // namespace resonator

class Effect : public Processor {
 public:
  enum {
    NOTE = 0U,
    STRUCTURE,
    MIX,
    BRIGHTNESS,
    DAMPING,
    POSITION,
    MODEL,
    POLYPHONY,
    NUM_PARAMS
  };

  // Everything big lives in SDRAM: the Part (strings' delay lines), the
  // Strummer, the reverb buffer and the sine table.
  static constexpr size_t kReverbBufferSize = 32768;  // uint16_t, as on Rings
  static constexpr size_t kSdramBytes =
      resonator::Align(sizeof(rings::Part)) +
      resonator::Align(sizeof(rings::Strummer)) +
      resonator::Align(kReverbBufferSize * sizeof(uint16_t)) +
      resonator::Align(LUT_SINE_SIZE * sizeof(float));

  uint32_t getBufferSize() const override final {
    return (kSdramBytes + sizeof(float) - 1) / sizeof(float);
  }

  void init(float* allocated_buffer) override final {
    uint8_t* p = reinterpret_cast<uint8_t*>(allocated_buffer);
    part_ = new (p) rings::Part();
    p += resonator::Align(sizeof(rings::Part));
    strummer_ = new (p) rings::Strummer();
    p += resonator::Align(sizeof(rings::Strummer));
    uint16_t* reverb_buffer = reinterpret_cast<uint16_t*>(p);
    p += resonator::Align(kReverbBufferSize * sizeof(uint16_t));
    float* sine = reinterpret_cast<float*>(p);
    FillSine(sine);
    rings::lut_sine = sine;

    part_->Init(reverb_buffer);
    strummer_->Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
    for (int i = 0; i < NUM_PARAMS; ++i) {
      setParameter(i, unit_header.common.params[i].init);
    }
    structure_ = target_structure_;
    brightness_ = target_brightness_;
    damping_ = target_damping_;
    position_ = target_position_;
    wet_ = target_wet_;
    reset();
  }

  void teardown() override final {
    part_ = nullptr;
    strummer_ = nullptr;
  }

  void reset() override final {
    std::fill(&block_in_[0], &block_in_[kBlock], 0.0f);
    std::fill(&block_out_[0], &block_out_[kBlock], 0.0f);
    std::fill(&block_aux_[0], &block_aux_[kBlock], 0.0f);
    pos_ = 0;
    in_level_ = 0.0f;
    input_hold_ = 0;
    strum_pending_ = false;
    touching_ = false;
    note_inhibit_ = 0;
    strummed_note_ = note_;
  }

  void process(const float* __restrict in, float* __restrict out,
               uint32_t frames) override final {
    if (!part_) {
      std::copy(in, in + 2 * frames, out);
      return;
    }
    for (uint32_t i = 0; i < frames; ++i, in += 2, out += 2) {
      // Rings is mono in, stereo (odd/even) out. Rings works on 24-sample
      // blocks, so the wet signal comes out one block (0.5 ms) late.
      float mono = 0.5f * (in[0] + in[1]);

      // Rings' input noise gate (rings.cc). Its envelope also tells whether
      // something is plugged in.
      float error = mono * mono - in_level_;
      in_level_ += error * (error > 0.0f ? 0.1f : 0.0001f);
      float gate = in_level_ <= kNoiseGateThreshold
          ? (1.0f / kNoiseGateThreshold) * in_level_ : 1.0f;
      block_in_[pos_] = gate * mono;

      float dry = std::min(1.0f, 2.0f * (1.0f - wet_));
      float wet = std::min(1.0f, 2.0f * wet_);
      out[0] = dry * in[0] + wet * block_out_[pos_];
      out[1] = dry * in[1] + wet * block_aux_[pos_];

      if (++pos_ == kBlock) {
        pos_ = 0;
        RenderBlock();
      }
    }
  }

  void setParameter(uint8_t index, int32_t value) override final {
    switch (index) {
      case NOTE: note_ = static_cast<float>(value); break;
      case STRUCTURE: target_structure_ = value * 0.0009995f; break;
      case MIX: target_wet_ = (value + 1000) * 0.0005f; break;
      case BRIGHTNESS: target_brightness_ = value * 0.001f; break;
      case DAMPING: target_damping_ = value * 0.001f; break;
      case POSITION: target_position_ = value * 0.001f; break;
      case MODEL: model_ = static_cast<rings::ResonatorModel>(value); break;
      case POLYPHONY: polyphony_ = 1 << value; break;
      default: break;
    }
  }

  const char* getParameterStrValue(uint8_t index,
                                   int32_t value) const override final {
    static const char* const models[] = {
        "Modal", "Sympath", "String", "FM Voice", "Chords", "String Rvb"};
    static const char* const polyphonies[] = {"1", "2", "4"};
    switch (index) {
      case MODEL:
        if (value >= 0 && value < rings::RESONATOR_MODEL_LAST)
          return models[value];
        break;
      case POLYPHONY:
        if (value >= 0 && value < 3) return polyphonies[value];
        break;
      default:
        break;
    }
    return nullptr;
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x,
                  uint32_t y) override final {
    (void)id;
    (void)x;
    (void)y;
    switch (phase) {
      case k_unit_touch_phase_began:
        touching_ = true;
        strum_pending_ = true;
        break;
      case k_unit_touch_phase_ended:
      case k_unit_touch_phase_cancelled:
        touching_ = false;
        break;
      default:
        break;
    }
  }

 private:
  static constexpr size_t kBlock = rings::kMaxBlockSize;
  static constexpr float kNoiseGateThreshold = 0.00003f;
  // Input counts as unplugged after 1.5 s below the noise gate threshold.
  static constexpr int32_t kInputHoldBlocks =
      static_cast<int32_t>(1.5f * resonator::kSampleRate / kBlock);
  // Same 10 ms inter-onset interval as the Rings strummer.
  static constexpr int32_t kNoteStrumInhibitBlocks =
      static_cast<int32_t>(0.01f * resonator::kSampleRate / kBlock);

  // sin(2 pi i / 4096), 5121 entries, the table Rings' resources.py generates.
  static void FillSine(float* table) {
    for (size_t i = 0; i < LUT_SINE_SIZE; ++i) {
      // Reduce to x in [-pi/2, pi/2] by quarter-wave symmetry, then Taylor to
      // x^11 (error < 1e-7 there).
      size_t k = i & 4095;
      float x;
      if (k <= 1024) {
        x = k;
      } else if (k <= 3072) {
        x = 2048.0f - k;
      } else {
        x = static_cast<float>(k) - 4096.0f;
      }
      x *= 6.283185307f / 4096.0f;
      float x2 = x * x;
      table[i] = x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f *
          (1.0f - x2 / 72.0f * (1.0f - x2 / 110.0f)))));
    }
  }

  void RenderBlock() {
    // Parameters move once per block, with the same one-pole smoothing Rings
    // applies to its pots.
    structure_ += 0.1f * (target_structure_ - structure_);
    brightness_ += 0.1f * (target_brightness_ - brightness_);
    damping_ += 0.1f * (target_damping_ - damping_);
    position_ += 0.1f * (target_position_ - position_);
    wet_ += 0.1f * (target_wet_ - wet_);

    if (in_level_ > kNoiseGateThreshold) {
      input_hold_ = kInputHoldBlocks;
    } else if (input_hold_) {
      --input_hold_;
    }
    bool input_present = input_hold_ > 0;

    // Both setters reinitialize the voices, so only call them on a change
    // (set_model checks for one itself).
    if (part_->polyphony() != polyphony_) {
      part_->set_polyphony(polyphony_);
    }
    part_->set_model(model_);

    rings::Patch patch;
    patch.structure = structure_;
    patch.brightness = brightness_;
    patch.damping = damping_;
    patch.position = position_;

    // Chord selection from structure, with hysteresis (cv_scaler.cc).
    float chord = structure_ * static_cast<float>(rings::kNumChords - 1);
    float hysteresis = chord - chord_ > 0.0f ? -0.1f : 0.1f;
    chord_ = static_cast<int32_t>(chord + hysteresis + 0.5f);
    CONSTRAIN(chord_, 0, rings::kNumChords - 1);

    rings::PerformanceState performance_state;
    performance_state.internal_exciter = !input_present;
    performance_state.internal_strum = true;
    performance_state.internal_note = true;
    performance_state.tonic = note_;
    performance_state.note = 0.0f;
    performance_state.fm = 0.0f;
    performance_state.chord = chord_;
    performance_state.strum = false;
    // Onsets on the input strum (only when something is plugged in).
    strummer_->Process(input_present ? block_in_ : NULL, kBlock,
                       &performance_state);

    if (note_inhibit_) {
      --note_inhibit_;
    }
    if (strum_pending_) {
      strum_pending_ = false;
      performance_state.strum = true;
    } else if (touching_ && !note_inhibit_ &&
               std::fabs(note_ - strummed_note_) > 0.4f) {
      performance_state.strum = true;
    }
    if (performance_state.strum) {
      strummed_note_ = note_;
      note_inhibit_ = kNoteStrumInhibitBlocks;
    }

    part_->Process(performance_state, patch, block_in_, block_out_, block_aux_,
                   kBlock);
  }

  rings::Part* part_ = nullptr;
  rings::Strummer* strummer_ = nullptr;

  float block_in_[kBlock];
  float block_out_[kBlock];
  float block_aux_[kBlock];
  size_t pos_ = 0;

  float in_level_ = 0.0f;
  int32_t input_hold_ = 0;

  // Written by the UI callbacks, read once per block.
  float note_ = 48.0f;
  float target_structure_ = 0.25f;
  float target_brightness_ = 0.5f;
  float target_damping_ = 0.5f;
  float target_position_ = 0.5f;
  float target_wet_ = 0.75f;
  rings::ResonatorModel model_ = rings::RESONATOR_MODEL_MODAL;
  int32_t polyphony_ = 1;
  volatile bool strum_pending_ = false;
  volatile bool touching_ = false;

  float structure_ = 0.25f;
  float brightness_ = 0.5f;
  float damping_ = 0.5f;
  float position_ = 0.5f;
  float wet_ = 0.75f;
  int32_t chord_ = 0;
  float strummed_note_ = 48.0f;
  int32_t note_inhibit_ = 0;
};
