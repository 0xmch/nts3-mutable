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
// Sample rate conversion filters.

#ifndef WARPS_DSP_SAMPLE_RATE_CONVERSION_FILTERS_H_
#define WARPS_DSP_SAMPLE_RATE_CONVERSION_FILTERS_H_

#include "warps/dsp/rate.h"

namespace warps {

// nts3-mutable: the converters depend on Warps' rate (dsp/rate.h). Upstream's,
// at 96 kHz:
#if WARPS_SAMPLE_RATE == 96000
// Generated with:
// 6 * scipy.signal.remez(48, [0, 0.060000 / 6, 0.5 / 6, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 6, 48> {
  inline float Read(int32_t i) const {
    static const float h[] = {
       4.357278576e-04, -2.297029461e-03, -4.703810602e-03, -8.774604727e-03,
      -1.433899145e-02, -2.112793398e-02, -2.853108802e-02, -3.552868193e-02,
      -4.069862931e-02, -4.228981313e-02, -3.836519645e-02, -2.700780696e-02,
      -6.569014106e-03,  2.407089704e-02,  6.526452513e-02,  1.164165703e-01,
       1.758932961e-01,  2.410483237e-01,  3.083744498e-01,  3.737697127e-01,
       4.328923682e-01,  4.815728403e-01,  5.162355916e-01,  5.342582974e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(48, [0, 0.060000 / 6, 0.5 / 6, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 6, 48> {
  inline float Read(int32_t i) const {
    static const float h[] = {
       7.262130960e-05, -3.828382434e-04, -7.839684337e-04, -1.462434121e-03,
      -2.389831909e-03, -3.521322331e-03, -4.755181337e-03, -5.921446989e-03,
      -6.783104885e-03, -7.048302188e-03, -6.394199409e-03, -4.501301159e-03,
      -1.094835684e-03,  4.011816173e-03,  1.087742085e-02,  1.940276171e-02,
       2.931554935e-02,  4.017472062e-02,  5.139574163e-02,  6.229495212e-02,
       7.214872804e-02,  8.026214006e-02,  8.603926526e-02,  8.904304957e-02,
    };
    return h[i];
  }
};

// Generated with:
// 4 * scipy.signal.remez(48, [0, 0.105000 / 4, 0.5 / 4, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 4, 48> {
  inline float Read(int32_t i) const {
    static const float h[] = {
      -6.014371929e-04, -1.116027480e-03, -1.547569918e-03, -1.288608084e-03,
       2.786886230e-04,  3.529342828e-03,  8.203156385e-03,  1.308970614e-02,
       1.600199910e-02,  1.419074690e-02,  5.231038872e-03, -1.177915684e-02,
      -3.506738553e-02, -5.953252182e-02, -7.699933415e-02, -7.757902368e-02,
      -5.198496872e-02,  5.703716839e-03,  9.559598586e-02,  2.106660616e-01,
       3.371310483e-01,  4.566603688e-01,  5.500087786e-01,  6.012053946e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(48, [0, 0.105000 / 4, 0.5 / 4, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 4, 48> {
  inline float Read(int32_t i) const {
    static const float h[] = {
      -1.503592982e-04, -2.790068700e-04, -3.868924795e-04, -3.221520211e-04,
       6.967215575e-05,  8.823357070e-04,  2.050789096e-03,  3.272426536e-03,
       4.000499774e-03,  3.547686724e-03,  1.307759718e-03, -2.944789209e-03,
      -8.766846381e-03, -1.488313045e-02, -1.924983354e-02, -1.939475592e-02,
      -1.299624218e-02,  1.425929210e-03,  2.389899647e-02,  5.266651541e-02,
       8.428276207e-02,  1.141650922e-01,  1.375021946e-01,  1.503013486e-01,
    };
    return h[i];
  }
};

// Generated with:
// 3 * scipy.signal.remez(36, [0, 0.050000 / 3, 0.5 / 3, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 3, 36> {
  inline float Read(int32_t i) const {
    static const float h[] = {
       2.111177486e-04,  9.399136027e-04,  2.516356933e-03,  4.847507152e-03,
       6.912087023e-03,  6.524576194e-03,  8.579855461e-04, -1.203466052e-02,
      -3.103696515e-02, -5.013495031e-02, -5.827142630e-02, -4.183809689e-02,
       1.038391226e-02,  1.014554664e-01,  2.222529437e-01,  3.515426263e-01,
       4.610075226e-01,  5.238640837e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(36, [0, 0.050000 / 3, 0.5 / 3, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 3, 36> {
  inline float Read(int32_t i) const {
    static const float h[] = {
       7.037258286e-05,  3.133045342e-04,  8.387856444e-04,  1.615835717e-03,
       2.304029008e-03,  2.174858731e-03,  2.859951820e-04, -4.011553507e-03,
      -1.034565505e-02, -1.671165010e-02, -1.942380877e-02, -1.394603230e-02,
       3.461304086e-03,  3.381848881e-02,  7.408431457e-02,  1.171808754e-01,
       1.536691742e-01,  1.746213612e-01,
    };
    return h[i];
  }
};


#else
// At 48 kHz, from tools/warps_src_filters.py, matched to upstream's in Hz.
// Cross-modulation, 6x: flat to 15 kHz (upstream: -1.7 dB at 15 kHz, its
// stopband starting at 48 kHz), -63 dB from 24 kHz (upstream -68 dB). Vocoder
// filter bank: the mid group runs at 24 kHz and the low group at 8 kHz, with
// upstream's latencies, so bands of different groups still sum with Warps'
// phases at the crossovers; within 1 dB of upstream's response over the mid
// bands, and the low filter is upstream's (840 Hz passband, 4 kHz stopband).
// Generated with:
// 6 * scipy.signal.remez(108, [0, 0.300000 / 6, 0.5 / 6, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 6, 108> {
  inline float Read(int32_t i) const {
    static const float h[] = {
      -2.071435050e-03,  3.649924707e-04,  9.411315769e-04,  1.757747484e-03,
       2.606257061e-03,  3.221435020e-03,  3.326512981e-03,  2.697265632e-03,
       1.231359860e-03, -9.918358870e-04, -3.681161244e-03, -6.349217281e-03,
      -8.379917690e-03, -9.143993571e-03, -8.140091367e-03, -5.137788704e-03,
      -2.929812060e-04,  5.778500460e-03,  1.207710189e-02,  1.731499678e-02,
       2.015934347e-02,  1.950147789e-02,  1.473955419e-02,  6.006093530e-03,
      -5.716106972e-03, -1.863590569e-02, -3.037221747e-02, -3.833302089e-02,
      -4.020465005e-02, -3.446626861e-02, -2.083699769e-02, -5.513925152e-04,
       2.361912180e-02,  4.764581620e-02,  6.679524841e-02,  7.642644212e-02,
       7.288083266e-02,  5.434031956e-02,  2.148162062e-02, -2.221302305e-02,
      -7.058801267e-02, -1.154186902e-01, -1.474376552e-01, -1.576564948e-01,
      -1.387914246e-01, -8.657678937e-02, -7.361373495e-04,  1.145623308e-01,
       2.508760044e-01,  3.962957882e-01,  5.368104287e-01,  6.580324598e-01,
       7.470577890e-01,  7.941752367e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(108, [0, 0.300000 / 6, 0.5 / 6, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 6, 108> {
  inline float Read(int32_t i) const {
    static const float h[] = {
      -3.452391750e-04,  6.083207845e-05,  1.568552628e-04,  2.929579140e-04,
       4.343761769e-04,  5.369058367e-04,  5.544188301e-04,  4.495442720e-04,
       2.052266433e-04, -1.653059812e-04, -6.135268740e-04, -1.058202880e-03,
      -1.396652948e-03, -1.523998928e-03, -1.356681895e-03, -8.562981174e-04,
      -4.883020100e-05,  9.630834099e-04,  2.012850314e-03,  2.885832797e-03,
       3.359890579e-03,  3.250246314e-03,  2.456592366e-03,  1.001015588e-03,
      -9.526844953e-04, -3.105984282e-03, -5.062036245e-03, -6.388836816e-03,
      -6.700775009e-03, -5.744378102e-03, -3.472832948e-03, -9.189875254e-05,
       3.936520300e-03,  7.940969367e-03,  1.113254140e-02,  1.273774035e-02,
       1.214680544e-02,  9.056719926e-03,  3.580270104e-03, -3.702170508e-03,
      -1.176466878e-02, -1.923644837e-02, -2.457294253e-02, -2.627608247e-02,
      -2.313190411e-02, -1.442946490e-02, -1.226895582e-04,  1.909372180e-02,
       4.181266740e-02,  6.604929803e-02,  8.946840478e-02,  1.096720766e-01,
       1.245096315e-01,  1.323625395e-01,
    };
    return h[i];
  }
};

// Generated with:
// 2 * scipy.signal.remez(18, [0, 0.150000 / 2, 0.5 / 2, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 2, 18> {
  inline float Read(int32_t i) const {
    static const float h[] = {
       6.094602917e-03,  1.635361782e-02,  8.202182223e-03, -3.425017879e-02,
      -8.293598180e-02, -5.195788543e-02,  1.247352481e-01,  4.005852846e-01,
       6.131731104e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(18, [0, 0.150000 / 2, 0.5 / 2, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 2, 18> {
  inline float Read(int32_t i) const {
    static const float h[] = {
       3.047301458e-03,  8.176808909e-03,  4.101091112e-03, -1.712508939e-02,
      -4.146799090e-02, -2.597894272e-02,  6.236762404e-02,  2.002926423e-01,
       3.065865552e-01,
    };
    return h[i];
  }
};

// Generated with:
// 3 * scipy.signal.remez(36, [0, 0.105000 / 3, 0.5 / 3, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 3, 36> {
  inline float Read(int32_t i) const {
    static const float h[] = {
      -6.540149897e-04, -1.372439301e-03, -1.403679833e-03,  6.945376187e-04,
       5.722618277e-03,  1.231914517e-02,  1.608292649e-02,  1.070446726e-02,
      -8.407394261e-03, -3.922063164e-02, -6.970751383e-02, -7.905219194e-02,
      -4.468066236e-02,  4.687652432e-02,  1.902288068e-01,  3.580476604e-01,
       5.077261221e-01,  5.960957197e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(36, [0, 0.105000 / 3, 0.5 / 3, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 3, 36> {
  inline float Read(int32_t i) const {
    static const float h[] = {
      -2.180049966e-04, -4.574797671e-04, -4.678932777e-04,  2.315125396e-04,
       1.907539426e-03,  4.106381724e-03,  5.360975498e-03,  3.568155754e-03,
      -2.802464754e-03, -1.307354388e-02, -2.323583794e-02, -2.635073065e-02,
      -1.489355412e-02,  1.562550811e-02,  6.340960227e-02,  1.193492201e-01,
       1.692420407e-01,  1.986985732e-01,
    };
    return h[i];
  }
};
#endif  // WARPS_SAMPLE_RATE

}  // namespace warps

#endif  // WARPS_DSP_SAMPLE_RATE_CONVERSION_FILTERS_H_
