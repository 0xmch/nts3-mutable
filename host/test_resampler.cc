// Round trip 48 -> 32 -> 48 kHz through mi_glue's resamplers: gain and error
// for in-band tones (best integer delay), and rejection above 16 kHz.
#include <cmath>
#include <cstdio>
#include <vector>
#include "../units/common/mi_glue.h"

// Output over input level, in dB, of one tone through the round trip.
double RoundTripGain(const float* proto, float f) {
  mi_glue::Downsampler down;
  mi_glue::Upsampler up;
  static float down_storage[mi_glue::Downsampler::kStorageSize];
  static float up_storage[mi_glue::Upsampler::kStorageSize];
  down.Init(proto, down_storage);
  up.Init(proto, up_storage);
  const int n = 24000;
  std::vector<float> fifo(40, 0.0f);
  size_t read = 0;
  double si = 0, so = 0;
  for (int i = 0; i < n; ++i) {
    float x = 0.5f * std::sin(2.0 * M_PI * f * i / 48000.0);
    float l, r;
    if (down.Process(x, x, &l, &r)) fifo.push_back(l);
    float y = 0.0f;
    if (up.needed()) y = read < fifo.size() ? fifo[read++] : 0.0f;
    up.Process(y, y, &l, &r);
    if (i >= 4000) { si += x * x; so += l * l; }
  }
  return 10 * std::log10(so / si + 1e-30);
}

int main() {
  float proto[mi_glue::kProtoTaps];
  mi_glue::DesignPrototype(proto);
  for (float f : {100.0f, 1000.0f, 5000.0f, 10000.0f, 13000.0f, 15000.0f, 17000.0f, 20000.0f}) {
    mi_glue::Downsampler down;
    mi_glue::Upsampler up;
    static float down_storage[mi_glue::Downsampler::kStorageSize];
    static float up_storage[mi_glue::Upsampler::kStorageSize];
    down.Init(proto, down_storage);
    up.Init(proto, up_storage);
    const int n = 48000;
    std::vector<float> in(n), out(n);
    std::vector<float> fifo;
    size_t read = 0;
    for (int i = 0; i < 40; ++i) fifo.push_back(0.0f);  // prefill
    for (int i = 0; i < n; ++i) {
      in[i] = 0.5f * std::sin(2.0 * M_PI * f * i / 48000.0);
      float l, r;
      if (down.Process(in[i], in[i], &l, &r)) fifo.push_back(l);
      float x = 0.0f;
      if (up.needed()) x = read < fifo.size() ? fifo[read++] : 0.0f;
      up.Process(x, x, &l, &r);
      out[i] = l;
    }
    double best_err = 1e9, gain = 0; int best_d = 0;
    for (int d = 0; d < 400; ++d) {
      double e = 0, s = 0, o = 0;
      for (int i = 8000; i < n - 400; ++i) { double err = out[i + d] - in[i]; e += err * err; s += in[i] * in[i]; o += out[i + d] * out[i + d]; }
      if (e < best_err) { best_err = e; best_d = d; gain = std::sqrt(o / s); }
      if (d == 0) { /* keep */ }
      best_err = std::min(best_err, e);
      (void)s;
    }
    double s = 0; for (int i = 8000; i < n - 400; ++i) s += in[i] * in[i];
    std::printf("%6.0f Hz: gain %+6.2f dB, error %6.1f dB, delay %d samples\n", f, 20 * std::log10(gain), 10 * std::log10(best_err / s), best_d);
  }
  // Worst rejection from 17 kHz up (aliases included), in 10 Hz steps.
  double worst = -1e9, at = 0;
  for (float f = 17000.0f; f < 24000.0f; f += 10.0f) {
    double g = RoundTripGain(proto, f);
    if (g > worst) { worst = g; at = f; }
  }
  std::printf("from 17 kHz up: worst %.1f dB at %.0f Hz\n", worst, at);
}
