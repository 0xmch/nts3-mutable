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
// Search for stretch/shift splicing points by maximizing correlation.
// Correlation is computed by XOR-ing the bit sign of samples - this allows
// 32 samples to be matched in one single XOR operation.

#ifndef CLOUDS_DSP_CORRELATOR_H_
#define CLOUDS_DSP_CORRELATOR_H_

#include "stmlib/stmlib.h"

namespace clouds {
  
class Correlator {
 public:
  Correlator() { }
  ~Correlator() { }
  
  void Init(uint32_t* source, uint32_t* destination);

  void StartSearch(int32_t size, int32_t offset, int32_t increment);
  
  inline int32_t best_match() const {
    return offset_ + (best_match_ * (increment_ >> 4) >> 12);
  }

  inline void EvaluateSomeCandidates() {
    size_t num_candidates = (size_ >> 2) + 16;
    // nts3-mutable: at most kMaxWordsPerCall words compared per call (about
    // 13% of a render period). On the module this runs in the main loop; in
    // an NTS-3 unit it runs in the audio callback, where a quarter of a
    // 4096-sample search (131k words) took 1.5 render periods. Searches over
    // windows up to about 1600 samples still finish before WSOLA reads the
    // result; longer ones are cut short, as they are on the module, whose
    // main loop does not finish them in time either.
    const size_t kMaxWordsPerCall = 4096;
    const size_t max_candidates = kMaxWordsPerCall / ((size_ >> 5) + 1);
    if (num_candidates > max_candidates) {
      num_candidates = max_candidates;
    }
    while (num_candidates) {
      EvaluateNextCandidate();
      --num_candidates;
    }
  }

  void EvaluateNextCandidate();

  inline uint32_t* source() { return source_; }
  inline uint32_t* destination() { return destination_; }
  inline int32_t candidate() { return candidate_; }

  inline bool done() { return done_; }

  // nts3-mutable: abandons the search in progress (its result was used).
  inline void Stop() { done_ = true; }
  
 private:
  uint32_t* source_;
  uint32_t* destination_;
  
  int32_t offset_;
  int32_t increment_;
  int32_t size_;
  int32_t candidate_;

  uint32_t best_score_;
  int32_t best_match_;
  
  int32_t trace_;
  
  bool done_;
  
  DISALLOW_COPY_AND_ASSIGN(Correlator);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_CORRELATOR_H_
