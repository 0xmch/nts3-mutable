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
# Warps' vocoder filter bank coefficients for a given sample rate.
#
# Python 3 port of pichenettes/eurorack warps/resources/filter_bank.py (MIT,
# Emilie Gillet): same band layout (20 third-octave bands from 87 Hz, three
# groups: decimated by 12, 3 and 1 at 96 kHz, by 6, 2 and 1 at 48 kHz, see
# warps/dsp/filter_bank.h), same filter designs, delay estimate and
# empirical fixes, plots dropped, poles paired explicitly (see
# conjugate_pairs). Prints C arrays in the layout of warps/resources.cc. At
# 96000 it reproduces the shipped table.
#
#   uv run --with numpy --with scipy tools/warps_filter_bank.py 48000

import sys

import numpy
import scipy.signal


def pole_pair_to_f_fq(pole_pair):
  fq = 1 - pole_pair.prod()
  f = -(2 - fq - (pole_pair.sum())) ** 0.5
  return f.real, fq.real


def conjugate_pairs(poles, mode):
  # Recent scipy versions no longer list a pole next to its conjugate: pair
  # them explicitly, in the section order of the shipped table.
  upper = sorted((p for p in poles if p.imag > 0), key=lambda p: p.real)
  pairs = [numpy.array([p, p.conjugate()]) for p in upper]
  fq = lambda pair: pole_pair_to_f_fq(pair)[1]
  return sorted(pairs, key=fq, reverse=(mode == 'bp'))


def modified_chamberlin(f, fq, x, mode='bp'):
  lp = 0.0
  bp = 0.0
  y = numpy.zeros(x.shape)
  x_ = 0.0
  coefficient = 1.0 if mode == 'bp' else 0.0
  for i in range(len(y)):
    lp += f * bp
    bp += -fq * bp - f * lp + (x[i] + x_ * coefficient)
    x_ = x[i]
    if mode == 'bp':
      y[i] = fq * bp
    elif mode == 'lp':
      y[i] = f * lp
    elif mode == 'hp':
      y[i] = x_ - lp * f - bp * fq
  return y


SAMPLE_RATE = int(sys.argv[1]) if len(sys.argv) > 1 else 96000
IR_SIZE = 2048

# Decimation of the low and mid groups: 8 and 32 kHz at 96 kHz; 8 and 24 kHz
# at 48 kHz (kLowFactor * kMidFactor and kMidFactor in filter_bank.h).
LOW, MID = {96000: (12, 3), 48000: (6, 2)}[SAMPLE_RATE]
sample_rates = [SAMPLE_RATE / LOW] * 13
sample_rates += [SAMPLE_RATE / MID] * 6
sample_rates += [SAMPLE_RATE] * 1
num_bands = len(sample_rates)

interval = 2 ** (1 / 3.0)
first_frequency = 110 / interval
frequencies = first_frequency * (interval ** numpy.arange(0, num_bands))

filters = []
for index, (frequency, sr) in enumerate(zip(frequencies, sample_rates)):
  frequency = frequency / (sr * 0.5)

  if index == 0:
    w = frequency
    z, p, k = scipy.signal.cheby1(4, 0.5, w, 'lowpass', output='zpk')
    svf_mode = 'lp'
    gain = 1.0
  elif index == num_bands - 1:
    w = frequency
    z, p, k = scipy.signal.cheby1(4, 0.25, w, 'highpass', output='zpk')
    svf_mode = 'hp'
    gain = 21 * frequency
  else:
    w = [frequency / (interval ** 0.5), frequency * (interval ** 0.5)]
    z, p, k = scipy.signal.butter(2, w, 'bandpass', output='zpk')
    svf_mode = 'bp'
    gain = 0.25

  out = numpy.eye(IR_SIZE, 1).ravel() * gain
  coefficients = [0, 0, 0]
  for pair in conjugate_pairs(p, svf_mode):
    f, fq = pole_pair_to_f_fq(pair)
    out = modified_chamberlin(f, fq, out, svf_mode)
    out = modified_chamberlin(f, fq, out, svf_mode)
    coefficients += [f, fq]

  delay = (numpy.arange(len(out)) * out * out).sum() / (out * out).sum()
  # Completely empirical fixes to the delay to maximize the flatness of the
  # total impulse response.
  if index == num_bands - 1:
    delay += 4

  coefficients[0] = SAMPLE_RATE / sr
  coefficients[1] = numpy.floor(delay)
  coefficients[2] = gain
  filters += [('%3.0f_%d' % (frequency * 0.5 * sr, sr), coefficients)]

for name, coefficients in filters:
  name = name.replace(' ', '_')
  print('const float fb_%s[] = {' % name)
  values = ['% .9e' % c for c in coefficients]
  for i in range(0, len(values), 4):
    print('  ' + ', '.join(values[i:i + 4]) + ',')
  print('};')
print()
print('const float* filter_bank_table[] = {')
for name, _ in filters:
  print('  fb_%s,' % name.replace(' ', '_'))
print('};')
