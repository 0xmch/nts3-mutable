// Copyright 2015 Emilie Gillet.
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
// Sample rate converter.

#ifndef WARPS_DSP_SAMPLE_RATE_CONVERTER_H_
#define WARPS_DSP_SAMPLE_RATE_CONVERTER_H_

#include "stmlib/stmlib.h"

#include <algorithm>

namespace warps {

enum SampleRateConversionDirection {
  SRC_UP,
  SRC_DOWN
};

template <SampleRateConversionDirection direction, int32_t ratio, int32_t length>
struct SRC_FIR { };

}

#include "warps/dsp/sample_rate_conversion_filters.h"

namespace warps {

// nts3-mutable: loops in place of upstream's fully unrolled templates
// (FilterState, Accumulator, PolyphaseStage), which do not fit the NTS-3's
// 32 KB unit. Each output sums the same products in the same order (from the
// oldest input to the newest), so the results are the same. The filters keep
// half of their symmetric impulse response (sample_rate_conversion_filters.h):
// tap t is stored at t below filter_size / 2, at filter_size - 1 - t above.
template<
    SampleRateConversionDirection direction,
    int32_t ratio,
    int32_t filter_size>
class SampleRateConverter { };

template<int32_t ratio, int32_t filter_size>
class SampleRateConverter<SRC_UP, ratio, filter_size> {
 private:
  enum {
    N = filter_size / ratio,
    K = ratio
  };
 
 public:
  SampleRateConverter() { }
  ~SampleRateConverter() { }

  inline void Init() {
    std::fill(&x_[0], &x_[N], 0);
  };

  inline int32_t delay() const { return filter_size / ratio / 2; }

  inline void Process(const float* in, float* out, size_t input_size) {
    SRC_FIR<SRC_UP, ratio, filter_size> ir;
    const int32_t half = filter_size / 2;
    while (input_size--) {
      // x_[0] holds the newest input.
      for (int32_t i = N - 1; i > 0; --i) {
        x_[i] = x_[i - 1];
      }
      x_[0] = *in++;
      // The K output phases side by side (independent sums, so that their
      // additions overlap); phase p meets input i through tap p + i * K.
      float y[K];
      for (int32_t p = 0; p < K; ++p) {
        y[p] = 0.0f;
      }
      for (int32_t i = N - 1; i >= 0; --i) {
        const float x = x_[i];
        const int32_t tap = i * K;
        if (tap >= half) {
#pragma GCC unroll 8
          for (int32_t p = 0; p < K; ++p) {
            y[p] = x * ir.Read(filter_size - 1 - tap - p) + y[p];
          }
        } else if (tap + K <= half) {
#pragma GCC unroll 8
          for (int32_t p = 0; p < K; ++p) {
            y[p] = x * ir.Read(tap + p) + y[p];
          }
        } else {
#pragma GCC unroll 8
          for (int32_t p = 0; p < K; ++p) {
            const int32_t t = tap + p;
            y[p] = x * ir.Read(t >= half ? filter_size - 1 - t : t) + y[p];
          }
        }
      }
      for (int32_t p = 0; p < K; ++p) {
        *out++ = y[p];
      }
    }
  }
  
 private:
  float x_[N];

  DISALLOW_COPY_AND_ASSIGN(SampleRateConverter);
};

template<int32_t ratio, int32_t filter_size>
class SampleRateConverter<SRC_DOWN, ratio, filter_size> {
 private:
  enum {
    N = filter_size,
    K = ratio
  };
 
 public:
  SampleRateConverter() { }
  ~SampleRateConverter() { }

  inline void Init() {
    std::fill(&x_[0], &x_[2 * N], 0);
    x_ptr_ = &x_[N - 1];
  };

  inline int32_t delay() const { return filter_size / 2; }

  // nts3-mutable: upstream's circular history only. Upstream switches to a
  // linear history for blocks of 8 filter lengths or more, which no Warps
  // block reaches (at most 144 samples into the 108-tap filter).
  inline void Process(const float* in, float* out, size_t input_size) {
    // When downsampling, the number of input samples must be a multiple
    // of the downsampling ratio.
    if ((input_size % ratio) != 0) {
      return;
    }

    SRC_FIR<SRC_DOWN, ratio, filter_size> ir;
    while (input_size) {
      for (int32_t i = 0; i < ratio; ++i) {
        x_ptr_[0] = x_ptr_[N] = *in++;
        --x_ptr_;
        if (x_ptr_ < x_) {
          x_ptr_ += N;
        }
      }
      input_size -= ratio;

      // x[i] is the input i samples old, which meets tap i.
      const float* x = &x_ptr_[1];
      float y = 0.0f;
      for (int32_t i = N - 1; i >= N / 2; --i) {
        y = x[i] * ir.Read(N - 1 - i) + y;
      }
      for (int32_t i = N / 2 - 1; i >= 0; --i) {
        y = x[i] * ir.Read(i) + y;
      }
      *out++ = y;
    }
  }
 
 private:
  float x_[2 * N];
  float* x_ptr_;

  DISALLOW_COPY_AND_ASSIGN(SampleRateConverter);
};

}  // namespace warps

#endif  // WARPS_DSP_SAMPLE_RATE_CONVERTER_H_
