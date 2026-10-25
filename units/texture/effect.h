// Texture: Mutable Instruments Clouds as an NTS-3 kaoss pad kit generic effect.
//
// The Clouds DSP (third_party/eurorack, MIT, Emilie Gillet) runs unchanged at
// its native 32 kHz, between a 48 -> 32 kHz and a 32 -> 48 kHz resampler. The
// dry signal never leaves 48 kHz: Clouds runs fully wet and the dry/wet
// crossfade (Clouds' own equal-power law) happens here.
//
// Pad: X = position, Y = size, depth = dry/wet. Touching the pad freezes the
// buffer and fires Clouds' trigger input; releasing it unfreezes. The playback
// mode (granular, stretch, looping delay) is fixed per unit by TEXTURE_MODE.

#pragma once

#include <algorithm>
#include <new>

#include "processor.h"
#include "unit_genericfx.h"

#include "clouds/dsp/granular_processor.h"
#include "clouds/resources.h"
#include "stmlib/dsp/dsp.h"

#include "../common/mi_glue.h"

#ifndef TEXTURE_MODE
#define TEXTURE_MODE 0  // clouds::PLAYBACK_MODE_GRANULAR
#endif
#ifndef TEXTURE_QUALITY
#define TEXTURE_QUALITY 0
#endif

namespace texture {
constexpr size_t Align(size_t size) { return (size + 7) & ~size_t(7); }
// Clouds' memory blocks: block_mem and block_ccm in clouds.cc.
constexpr size_t kLargeBufferSize = 118784;
constexpr size_t kSmallBufferSize = 65536 - 128;
constexpr size_t kBlock = clouds::kMaxBlockSize;  // 32 frames at 32 kHz
// Output FIFO between Clouds blocks and the upsampler: one block plus margin,
// so the upsampler never runs dry while the next block fills.
constexpr size_t kFifoPrefill = kBlock + 2;
constexpr size_t kFifoSize = 128;
}  // namespace texture

class Effect : public Processor {
 public:
  enum {
    POSITION = 0U,
    SIZE,
    MIX,
    PITCH,
    DENSITY,
    TEXTURE,
    FEEDBACK,
    REVERB,
    NUM_PARAMS
  };

  static constexpr size_t kSdramBytes =
      texture::Align(sizeof(clouds::GranularProcessor)) +
      texture::Align(texture::kLargeBufferSize) +
      texture::Align(texture::kSmallBufferSize) +
      texture::Align(LUT_SIN_SIZE * sizeof(float)) +
      texture::Align(LUT_WINDOW_SIZE * sizeof(float));

  uint32_t getBufferSize() const override final {
    return (kSdramBytes + sizeof(float) - 1) / sizeof(float);
  }

  void init(float* allocated_buffer) override final {
    uint8_t* p = reinterpret_cast<uint8_t*>(allocated_buffer);
    processor_ = new (p) clouds::GranularProcessor();
    p += texture::Align(sizeof(clouds::GranularProcessor));
    uint8_t* large = p;
    p += texture::Align(texture::kLargeBufferSize);
    uint8_t* small = p;
    p += texture::Align(texture::kSmallBufferSize);

    // Clouds' resources.py: lut_sin is sin(2 pi i / 1024) over 1.25 periods,
    // lut_window is 1 - (cos(pi t) + 1) / 2 = sin^2(pi t / 2) over [0, 1].
    float* sine = reinterpret_cast<float*>(p);
    mi_glue::FillSine(sine, LUT_SIN_SIZE, 1024);
    clouds::lut_sin = sine;
    p += texture::Align(LUT_SIN_SIZE * sizeof(float));
    float* window = reinterpret_cast<float*>(p);
    for (size_t i = 0; i < LUT_WINDOW_SIZE; ++i) {
      float s = mi_glue::SinTurns(0.25f * i / (LUT_WINDOW_SIZE - 1));
      window[i] = s * s;
    }
    clouds::lut_window = window;

    processor_->Init(large, texture::kLargeBufferSize, small,
                     texture::kSmallBufferSize);
    processor_->set_silence(false);
    processor_->set_bypass(false);
    // Clouds' quality: 0 is 16-bit stereo; 3 (Texture Lo-Fi) is mono, 8-bit
    // mu-law samples and a 16 kHz internal rate, with a longer buffer.
    processor_->set_quality(TEXTURE_QUALITY);
    processor_->set_playback_mode(
        static_cast<clouds::PlaybackMode>(TEXTURE_MODE));

    float prototype[mi_glue::kProtoTaps];
    mi_glue::DesignPrototype(prototype);
    down_.Init(prototype);
    up_.Init(prototype);

    for (int i = 0; i < NUM_PARAMS; ++i) {
      setParameter(i, unit_header.common.params[i].init);
    }
    position_ = target_position_;
    size_ = target_size_;
    pitch_ = target_pitch_;
    density_ = target_density_;
    texture_ = target_texture_;
    feedback_ = target_feedback_;
    reverb_ = target_reverb_;
    mix_ = target_mix_;
    reset();
  }

  void teardown() override final { processor_ = nullptr; }

  void reset() override final {
    n_in_ = 0;
    fifo_read_ = 0;
    fifo_write_ = texture::kFifoPrefill;
    std::fill(&fifo_l_[0], &fifo_l_[texture::kFifoSize], 0.0f);
    std::fill(&fifo_r_[0], &fifo_r_[texture::kFifoSize], 0.0f);
    touching_ = false;
    trigger_pending_ = false;
  }

  void process(const float* __restrict in, float* __restrict out,
               uint32_t frames) override final {
    if (!processor_) {
      std::copy(in, in + 2 * frames, out);
      return;
    }
    for (uint32_t i = 0; i < frames; ++i, in += 2, out += 2) {
      float l, r;
      if (down_.Process(in[0], in[1], &l, &r)) {
        block_in_[n_in_].l = ToShort(l);
        block_in_[n_in_].r = ToShort(r);
        if (++n_in_ == texture::kBlock) {
          n_in_ = 0;
          RenderBlock();
        }
      }
      float wet_l = 0.0f, wet_r = 0.0f;
      if (up_.needed()) {
        wet_l = fifo_l_[fifo_read_];
        wet_r = fifo_r_[fifo_read_];
        fifo_read_ = (fifo_read_ + 1) % texture::kFifoSize;
      }
      up_.Process(wet_l, wet_r, &wet_l, &wet_r);

      // Clouds' equal-power crossfade (granular_processor.cc). Running fully
      // wet, Clouds outputs its wet signal through SoftConvert, which
      // soft-limits at half level before the module's analog gain; 2 sqrt(2)
      // brings it back to unity. On the module that leaves the wet signal
      // about 9 dB of headroom above input level, which the NTS-3 output does
      // not have, so the wet signal goes through a soft knee into [-1, 1].
      // The dry signal skips both: the NTS-3 chains four effects, and Mix
      // fully dry should be transparent.
      mix_ += 0.001f * (target_mix_ - mix_);
      float fade_in = stmlib::Interpolate(clouds::lut_xfade_in, mix_, 16.0f);
      float fade_out = stmlib::Interpolate(clouds::lut_xfade_out, mix_, 16.0f);
      const float kSqrt2 = 1.4142136f;
      const float kWetScale = 2.828427f;
      wet_l = SoftKnee(kWetScale * wet_l);
      wet_r = SoftKnee(kWetScale * wet_r);
      out[0] = Clamp(kSqrt2 * (in[0] * fade_out + wet_l * fade_in));
      out[1] = Clamp(kSqrt2 * (in[1] * fade_out + wet_r * fade_in));
    }
  }

  void setParameter(uint8_t index, int32_t value) override final {
    switch (index) {
      case POSITION: target_position_ = value * 0.001f; break;
      case SIZE: target_size_ = value * 0.001f; break;
      case MIX: {
        // Blend knob scaling from cv_scaler.cc.
        float mix = (value + 1000) * 0.0005f * 1.05f - 0.025f;
        target_mix_ = std::min(1.0f, std::max(0.0f, mix));
        break;
      }
      case PITCH: target_pitch_ = static_cast<float>(value); break;
      case DENSITY: target_density_ = value * 0.001f; break;
      case TEXTURE: target_texture_ = value * 0.001f; break;
      case FEEDBACK: target_feedback_ = value * 0.001f; break;
      case REVERB: target_reverb_ = value * 0.001f; break;
      default: break;
    }
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x,
                  uint32_t y) override final {
    (void)id;
    (void)x;
    (void)y;
    switch (phase) {
      case k_unit_touch_phase_began:
        touching_ = true;
        trigger_pending_ = true;
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
  static inline short ToShort(float x) {
    int32_t s = static_cast<int32_t>(x * 32768.0f);
    return static_cast<short>(s < -32768 ? -32768 : (s > 32767 ? 32767 : s));
  }
  // Identity up to 0.75, then a curve with matching slope that approaches 1.
  static inline float SoftKnee(float x) {
    float a = x < 0.0f ? -x : x;
    if (a <= 0.75f) return x;
    float u = (a - 0.75f) * 4.0f;
    float y = 0.75f + 0.25f * u / (1.0f + u);
    return x < 0.0f ? -y : y;
  }
  static inline float Clamp(float x) {
    return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
  }

  void RenderBlock() {
    // Pot smoothing per block, with the coefficients of cv_scaler.cc.
    position_ += 0.05f * (target_position_ - position_);
    size_ += 0.01f * (target_size_ - size_);
    pitch_ += 0.01f * (target_pitch_ - pitch_);
    density_ += 0.01f * (target_density_ - density_);
    texture_ += 0.01f * (target_texture_ - texture_);
    feedback_ += 0.05f * (target_feedback_ - feedback_);
    reverb_ += 0.05f * (target_reverb_ - reverb_);

    clouds::Parameters* p = processor_->mutable_parameters();
    p->position = position_;
    p->size = size_;
    p->pitch = pitch_;
    p->density = density_;
    p->texture = texture_;
    p->dry_wet = 1.0f;
    p->stereo_spread = 0.5f;
    p->feedback = feedback_;
    p->reverb = reverb_;
    p->freeze = touching_;
    p->trigger = trigger_pending_;
    p->gate = touching_;
    trigger_pending_ = false;

    processor_->Prepare();
    processor_->Process(block_in_, block_out_, texture::kBlock);

    for (size_t i = 0; i < texture::kBlock; ++i) {
      fifo_l_[fifo_write_] = block_out_[i].l / 32768.0f;
      fifo_r_[fifo_write_] = block_out_[i].r / 32768.0f;
      fifo_write_ = (fifo_write_ + 1) % texture::kFifoSize;
    }
  }

  clouds::GranularProcessor* processor_ = nullptr;
  mi_glue::Downsampler down_;
  mi_glue::Upsampler up_;

  clouds::ShortFrame block_in_[texture::kBlock];
  clouds::ShortFrame block_out_[texture::kBlock];
  size_t n_in_ = 0;
  float fifo_l_[texture::kFifoSize];
  float fifo_r_[texture::kFifoSize];
  size_t fifo_read_ = 0;
  size_t fifo_write_ = 0;

  // Written by the UI callbacks, read once per block.
  float target_position_ = 0.0f;
  float target_size_ = 0.5f;
  float target_mix_ = 0.5f;
  float target_pitch_ = 0.0f;
  float target_density_ = 0.5f;
  float target_texture_ = 0.5f;
  float target_feedback_ = 0.0f;
  float target_reverb_ = 0.0f;
  volatile bool touching_ = false;
  volatile bool trigger_pending_ = false;

  float position_ = 0.0f;
  float size_ = 0.5f;
  float mix_ = 0.5f;
  float pitch_ = 0.0f;
  float density_ = 0.5f;
  float texture_ = 0.5f;
  float feedback_ = 0.0f;
  float reverb_ = 0.0f;
};
