// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// WSOLA playback.

#ifndef CLOUDS_DSP_WSOLA_SAMPLE_PLAYER_H_
#define CLOUDS_DSP_WSOLA_SAMPLE_PLAYER_H_

#include <algorithm>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "stmlib/stmlib.h"
#include "stmlib/dsp/units.h"

#include "clouds/dsp/audio_buffer.h"
#include "clouds/dsp/correlator.h"
#include "clouds/dsp/frame.h"
#include "clouds/dsp/window.h"
#include "clouds/dsp/parameters.h"
#include "clouds/resources.h"

namespace clouds {

const int32_t kMaxWSOLASize = 4096;

using namespace stmlib;

class WSOLASamplePlayer {
 public:
  WSOLASamplePlayer() { }
  ~WSOLASamplePlayer() { }
  
  void Init(
      Correlator* correlator,
      int32_t num_channels) {
    correlator_ = correlator;
    num_channels_ = num_channels;

    pitch_ = 0.0f;
    position_ = 0.0f;
    smoothed_pitch_ = 0.0f;

    windows_[0].Init();
    windows_[1].Init();

    next_pitch_ratio_ = 1.0f;
    correlator_loaded_ = true;
    load_stage_ = 0;
    search_source_ = 0;
    search_target_ = 0;
    
    window_size_ = kMaxWSOLASize / 2;
    env_phase_ = 0.0f;
    env_phase_increment_ = 0.5f;
    elapsed_ = 0;
  }
  
  template<Resolution resolution>
  void Play(
      const AudioBuffer<resolution>* buffer,
      const Parameters& parameters,
      float* out,
      size_t size) {
    elapsed_++;
    if (parameters.trigger) {
      env_phase_ = 0.0f;
      env_phase_increment_ = 1.0f / static_cast<float>(elapsed_);
      CONSTRAIN(env_phase_increment_, 0.0001f, 0.1f);
      elapsed_ = 0;
    }
    env_phase_ += env_phase_increment_;
    if (env_phase_ >= 1.0f) {
      env_phase_ = 1.0;
    }
    position_ = parameters.position;
    position_ += (1.0f - env_phase_) * (1.0f - position_);
    
    pitch_ = parameters.pitch;
    size_factor_ = parameters.size;
    
    if (windows_[0].done() && windows_[1].done()) {
      windows_[1].MarkAsRegenerated();
      ScheduleAlignedWindow(buffer, &windows_[0]);
    }
    
    while (size--) {
      // Sum the two windows.
      std::fill(&out[0], &out[kMaxNumChannels], 0);
      for (int32_t i = 0; i < 2; ++i) {
        windows_[i].OverlapAdd(buffer, out, num_channels_);
      }

      // Regenerate expired windows.
      for (int32_t i = 0; i < 2; ++i) {
        if (windows_[i].needs_regeneration()) {
          windows_[i].MarkAsRegenerated();
          ScheduleAlignedWindow(buffer, &windows_[1 - i]);
          windows_[1 - i].OverlapAdd(buffer, out, num_channels_);
        }
      }
      out += 2;
    }
  }
  
  // nts3-mutable: LoadCorrelator() reads at most kSignBitsPerCall samples per
  // call, resuming where it left off. On the module it runs in the main
  // loop; in an NTS-3 unit it runs in the audio callback, where reading the
  // three windows at once (up to 4900 samples, from SDRAM) took 70% of a
  // render period. It still finishes long before WSOLA reads the result.
  static const int32_t kSignBitsPerCall = 768;

  template<Resolution resolution>
  void StartSignBits(
      const AudioBuffer<resolution>* buffer,
      int32_t phase_increment,
      int32_t source,
      int32_t size,
      uint32_t* destination) {
    if (source < 0) {
      source += buffer->size();
    }
    reader_.source = source;
    reader_.size = size;
    reader_.phase = 0;
    reader_.increment = phase_increment;
    reader_.bits = 0;
    reader_.bit_counter = 0;
    reader_.num_samples = 0;
    reader_.destination = destination;
  }

  // Upstream's ReadSignBits(), resumable: true once the read is complete,
  // with the number of samples in reader_.num_samples. (The number of
  // channels is checked at run time, not a template parameter: the code
  // fits the 32 KB unit.)
  template<Resolution resolution>
  bool ContinueSignBits(
      const AudioBuffer<resolution>* buffer,
      int32_t* budget) {
    SignBitsReader& r = reader_;
    while ((r.phase >> 16) < r.size) {
      if (*budget <= 0) {
        return false;
      }
      --*budget;
      int32_t integral = r.source + (r.phase >> 16);
      uint16_t fractional = r.phase & 0xffff;
      float s = buffer[0].ReadLinear(integral, fractional);
      if (num_channels_ == 2) {
        s += buffer[1].ReadLinear(integral, fractional);
      }
      r.bits |= s > 0.0f ? 1 : 0;
      if ((r.bit_counter & 0x1f) == 0x1f) {
        r.destination[r.bit_counter >> 5] = r.bits;
        r.num_samples += 32;
      }
      ++r.bit_counter;
      r.bits <<= 1;
      r.phase += r.increment;
    }
    while (r.bit_counter & 0x1f) {
      if ((r.bit_counter & 0x1f) == 0x1f) {
        r.destination[r.bit_counter >> 5] = r.bits;
        r.num_samples += 32;
      }
      ++r.bit_counter;
      r.bits <<= 1;
    }
    return true;
  }

  template<Resolution resolution>
  void LoadCorrelator(const AudioBuffer<resolution>* buffer) {
    if (correlator_loaded_) {
      return;
    }
    int32_t budget = kSignBitsPerCall;
    if (load_stage_ == 0) {
      float stride = window_size_ / 2048.0f;
      CONSTRAIN(stride, 1.0f, 2.0f);
      stride *= 65536.0f;
      load_increment_ = static_cast<int32_t>(
            stride * (next_pitch_ratio_ < 1.25f ? 1.25f : next_pitch_ratio_));
      StartSignBits(
          buffer,
          load_increment_,
          search_source_,
          window_size_,
          correlator_->source());
      load_stage_ = 1;
    }
    if (load_stage_ == 1) {
      if (!ContinueSignBits(buffer, &budget)) {
        return;
      }
      load_num_samples_ = reader_.num_samples;
      StartSignBits(
          buffer,
          load_increment_,
          search_target_ - window_size_,
          window_size_ * 2,
          correlator_->destination());
      load_stage_ = 2;
    }
    if (!ContinueSignBits(buffer, &budget)) {
      return;
    }
    correlator_->StartSearch(
        load_num_samples_,
        search_target_ - window_size_ + (window_size_ >> 1),
        load_increment_);
    correlator_loaded_ = true;
    load_stage_ = 0;
  }
 private:
  template<Resolution resolution>
  void ScheduleAlignedWindow(
      const AudioBuffer<resolution>* buffer,
      Window* window) {
    int32_t next_window_position = correlator_->best_match();
    correlator_loaded_ = false;
    load_stage_ = 0;  // nts3-mutable: a load in progress starts over
    correlator_->Stop();
    window->Start(
        buffer->size(),
        next_window_position - (window_size_ >> 1),
        window_size_,
        static_cast<uint32_t>(next_pitch_ratio_ * 65536.0f));
    
    float pitch_error = pitch_ - smoothed_pitch_;
    float pitch_error_sign = pitch_error < 0.0f ? -1.0 : 1.0;
    pitch_error *= pitch_error_sign;
    if (pitch_error >= 12.0f) {
      pitch_error = 12.0f;
    }
    smoothed_pitch_ += pitch_error * pitch_error_sign;
    float pitch_ratio = SemitonesToRatio(smoothed_pitch_);
    float inv_pitch_ratio = SemitonesToRatio(-smoothed_pitch_);
    next_pitch_ratio_ = pitch_ratio;
    
    float size_factor = SemitonesToRatio((size_factor_ - 1.0f) * 60.0f);
    int32_t new_window_size = static_cast<int32_t>(size_factor * kMaxWSOLASize);
    if (abs(new_window_size - window_size_) > 64) {
      int32_t error = (new_window_size - window_size_) >> 5;
      new_window_size = window_size_ + error;
      window_size_ = new_window_size - (new_window_size % 4);
    }
    
    // The center offset of the window we want to mix in.
    int32_t limit = buffer->size();
    limit -= static_cast<int32_t>(2.0f * window_size_ * inv_pitch_ratio);
    limit -= 2 * window_size_;
    if (limit < 0) {
      limit = 0;
    }
    
    float position = position_;
    int32_t target_position = buffer->head();
    target_position -= static_cast<int32_t>(limit * position);
    target_position -= window_size_;
    
    search_source_ = next_window_position;
    search_target_ = target_position;
  }
  
  Correlator* correlator_;

  Window windows_[2];

  int32_t window_size_;
  int32_t num_channels_;
  
  float pitch_;
  float smoothed_pitch_;
  float position_;
  float size_factor_;
  
  float next_pitch_ratio_;
  bool correlator_loaded_;

  // nts3-mutable: LoadCorrelator()'s progress (0 not started, 1 reading the
  // source, 2 the destination).
  struct SignBitsReader {
    int32_t source;
    int32_t size;
    int32_t phase;
    int32_t increment;
    uint32_t bits;
    uint32_t bit_counter;
    int32_t num_samples;
    uint32_t* destination;
  };
  SignBitsReader reader_;
  int32_t load_stage_;
  int32_t load_increment_;
  int32_t load_num_samples_;
  int32_t search_source_;
  int32_t search_target_;
  
  float env_phase_;
  float env_phase_increment_;
  int32_t elapsed_;
  
  DISALLOW_COPY_AND_ASSIGN(WSOLASamplePlayer);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_WSOLA_SAMPLE_PLAYER_H_
