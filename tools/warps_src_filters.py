# Copyright 2014 Emilie Gillet.
#
# Author: Emilie Gillet (emilie.o.gillet@gmail.com)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# 
# See http://creativecommons.org/licenses/MIT/ for more information.
#
# Warps' sample rate conversion filters: SRC_FIR specializations for
# warps/dsp/sample_rate_conversion_filters.h.
#
# Python 3 port of pichenettes/eurorack warps/tools/generate_src_filters.py
# (MIT, Emilie Gillet), plots dropped: each configuration is ratio, length,
# transition, and the filter is remez(length, [0, transition / ratio,
# 0.5 / ratio, 0.5], [1, 0]), normalized to unity DC gain. --report prints the
# passband edge, stopband edge and worst stopband gain instead.
#
#   uv run --with numpy --with scipy tools/warps_src_filters.py '2,24,0.0667'

import sys

import numpy
import scipy.signal


def design(ratio, length, transition):
  ir = scipy.signal.remez(length, [0, transition / ratio, 0.5 / ratio, 0.5], [1, 0])
  return ir / sum(ir)


def stopband_db(ir, ratio):
  n = 16384
  gain = 20 * numpy.log10(numpy.abs(numpy.fft.rfft(ir, n)) + 1e-30)
  f = numpy.arange(n // 2 + 1) / n
  return gain[f >= 0.5 / ratio].max()


report = '--report' in sys.argv
configurations = [a for a in sys.argv[1:] if not a.startswith('--')]
for configuration in ';'.join(configurations).split(';'):
  tokens = configuration.split(',')
  ratio, length = int(tokens[0]), int(tokens[1])
  transition = float(tokens[2])
  ir = design(ratio, length, transition)
  if report:
    print('ratio %d, %d taps, transition %.4f: stopband %.1f dB' % (
        ratio, length, transition, stopband_db(ir, ratio)))
    continue
  cmd = 'scipy.signal.remez(%d, [0, %f / %d, 0.5 / %d, 0.5], [1, 0])' % (
      length, transition, ratio, ratio)
  for up in [True, False]:
    scale = ratio if up else 1.0
    scaled = ir * scale
    print('\n// Generated with:')
    print('// %d * %s' % (scale, cmd))
    print('template<>')
    print('struct SRC_FIR<SRC_%s, %d, %d> {' % ('UP' if up else 'DOWN', ratio, length))
    print('  inline float Read(int32_t i) const {')
    print('    static const float h[] = {')
    for i in range(0, length // 2, 4):
      print('      ' + ', '.join('% 16.9e' % scaled[j] for j in range(i, min(length // 2, i + 4))) + ',')
    print('    };')
    print('    return h[i];')
    print('  }')
    print('};')
