// Round trip 48 -> 32 -> 48 kHz through mi_glue's resamplers: gain and error
// for in-band tones (best integer delay), and rejection above 16 kHz.
#include <cmath>
#include <cstdio>
#include <vector>
#include "../units/common/mi_glue.h"

int main() {
  float proto[mi_glue::kProtoTaps];
  mi_glue::DesignPrototype(proto);
  for (float f : {100.0f, 1000.0f, 5000.0f, 10000.0f, 13000.0f, 15000.0f, 17000.0f, 20000.0f}) {
    mi_glue::Downsampler down;
    mi_glue::Upsampler up;
    down.Init(proto);
    up.Init(proto);
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
}
