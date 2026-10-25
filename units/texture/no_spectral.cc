// The Texture units never run Clouds' spectral mode: its phase vocoder does a
// whole 4096-point FFT frame in one call, which a render callback cannot
// absorb, and its code would not fit the 32 KB unit image. GranularProcessor
// still references the phase vocoder, so these empty definitions stand in for
// clouds/dsp/pvoc/*.cc. TEXTURE_MODE never selects PLAYBACK_MODE_SPECTRAL, so
// none of them is ever called.

#include "clouds/dsp/pvoc/phase_vocoder.h"

namespace clouds {

void PhaseVocoder::Init(void** buffer, size_t* buffer_size,
                        const float* large_window_lut, size_t largest_fft_size,
                        int32_t num_channels, int32_t resolution,
                        float sample_rate) {}

void PhaseVocoder::Process(const Parameters& parameters,
                           const FloatFrame* input, FloatFrame* output,
                           size_t size) {}

void PhaseVocoder::Buffer() {}

}  // namespace clouds
