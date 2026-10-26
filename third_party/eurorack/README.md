Subset of Mutable Instruments' eurorack firmware and stmlib, by Emilie Gillet,
MIT licensed (see the header of each file and stmlib/LICENSE).

- eurorack f4cfa7697095a28d292bff758beb51deb49b5a86
- stmlib d18def816c51d1da0c108236928b2bbd25c17481

Files are verbatim except:
- rings/resources.{h,cc}, clouds/resources.{h,cc}: the big sine and window
  tables are pointers filled at init in SDRAM from upstream's formulas (in
  float: Clouds' within 1.2e-7 and 3e-7 of the shipped values, not bit for
  bit), and the unused table indexes are dropped.
- clouds/dsp/window.h: restores `done_ = false;` in Window::Start(). On
  upstream master since 2023-03-13 (merge d1d8839c11 of fbb53ba24b and
  0e3756fd56, which each dropped one copy of a duplicated line) Clouds' stretch
  mode never starts a window and stays silent. The 2015 firmware has the line.
- clouds/drivers/debug_pin.h, warps/drivers/debug_pin.h: empty stand-ins for
  the STM32 profiling pin (upstream's TEST branch, with its notice).
- clouds/dsp/granular_processor.cc: with NTS3_METER_PROFILE (metered
  diagnostic builds only, units/common/meter.h) it times LoadCorrelator() and
  EvaluateSomeCandidates(); other builds compile upstream's code.
- warps/resources.{h,cc}: lut_sin, the cross-fade, MIDI-to-frequency and wave
  tables are pointers filled at init in SDRAM; lut_bipolar_fold (measured
  data) ships as Q15 second differences in 8 bits, decoded at init, within
  1.4e-5 of the original.
- warps/dsp/modulator.cc: the wavefolder clamps its table index. Past full
  scale (high drive, or the pulse carrier's gain in the cross-modulation to
  vocoder crossfade) upstream reads tens of KB outside the table; harmless in
  the module's flash, a possible bus fault in the NTS-3's SDRAM.
- Warps at 48 kHz (warps/dsp/rate.h, WARPS_SAMPLE_RATE=48000, used by the Meta
  Mod unit; the default 96000 builds upstream's code paths and tables):
  - per-sample one-pole constants tuned at 96 kHz (limiter.h, modulator.h's
    amplifier, oscillator.cc) go through WARPS_ONE_POLE, which keeps their
    time constants;
  - sample_rate_conversion_filters.h: 48 kHz converters from
    tools/warps_src_filters.py, matched to upstream's in Hz. The 6x
    cross-modulation filter is longer (108 taps: flat to 15 kHz, -63 dB from
    24 kHz), so modulator.{h,cc} delay the raw modulator mixed back in the
    vocoder transition by 14 samples. Upstream's offset between the two would
    take about 13.5 (the decimator's output phase included), so they end up
    half a sample further apart than upstream's, which moves the comb where
    the two paths cancel in that transition up by about 1 kHz;
  - filter_bank.{h,cc}: groups decimated by 2 and 3 (24 and 8 kHz, upstream
    32 and 8 kHz) with upstream's converter latencies, and the 2.7 ms delay
    compensation cap kept in time;
  - filter_bank_48k.inc (resources.cc): the vocoder bands at those rates, from
    tools/warps_filter_bank.py, which reproduces the shipped 96 kHz table up
    to the order of the two filter sections in two bands.
  Against the 96 kHz build in the host harness, third-octave levels from
  100 Hz to 16 kHz agree within 0.8 dB in cross-modulation and 2.7 dB in the
  vocoder (medians 0.5 dB or less).
- Warps' memory, for the NTS-3: on the device, parts of Warps' state (a
  word's low half, now and then) did not read back from SDRAM once written
  back from the D-cache, and a corrupted vocoder delay line pointer froze the
  device. Units keep their DSP objects in their own RAM and only plain buffers
  in SDRAM, as every published NTS-3 unit does, and the unit image (code and
  RAM) is capped at 32 KB, so:
  - filter_bank.{h,cc}, vocoder.{h,cc}, modulator.{h,cc}: the buffers
    (FilterBank's tmp_, samples_ and delay_buffer_, Vocoder's tmp_,
    Modulator's internal_modulation_, buffer_, transition_modulator_ and
    src_buffer_) are pointers handed out at Init() by a stmlib
    BufferAllocator (kModulatorBufferSize floats in all); the rest of the
    state stays in the object;
  - sample_rate_converter.h: loops in place of the fully unrolled converter
    templates, summing the same products in the same order (outputs identical
    with floating-point contraction off; the upsampler computes its phases
    side by side), and the downsampler keeps only its circular-history path,
    which every Warps block size uses; sample_rate_conversion_filters.h reads
    taps by run-time index (Read(i), from tools/warps_src_filters.py);
  - modulator.h: WARPS_PROCESS_EASTER_EGG (default 1); 0 drops the easter egg
    from Process() for builds that never enable it (Meta Mod), and
    ProcessEasterEgg() stays available (Freq Shifter).
  The host harness renders all test scripts byte for byte as before these
  changes.
- The same split for Rings and Clouds (Resonator, Texture), and the same
  reason:
  - stmlib/dsp/delay_line.h: the samples live in memory handed over once with
    set_line() (before Init(), which only resets); the indices stay in the
    object;
  - rings/dsp/string.h, plucker.h, note_filter.h, resonator.h: setters for
    their delay lines' samples and the mode filters (floats only);
    part.{h,cc}: Init() takes a BufferAllocator for them and for the block
    buffers (kPartBufferBytes);
  - clouds/dsp/sample_rate_converter.h: the coefficients and the history come
    from a BufferAllocator; granular_processor.{h,cc}: Init() takes one for
    the block buffers, the tail buffer and the converters' arrays
    (kGranularProcessorBufferBytes); the converters' types are named
    (DownConverter, UpConverter) for that size;
  - Texture Stretch's search for the next window's alignment, which runs in
    the module's main loop, runs in the audio callback here, where it took up
    to 1.5 render periods: wsola_sample_player.h reads the sign bits of the
    windows (LoadCorrelator) at most 768 samples per call, resuming across
    calls, and clouds/dsp/correlator.h compares at most 4096 words per call
    (Stop() abandons a search whose result was used). Searches over windows
    up to about 1600 samples still finish before WSOLA reads their result;
    longer ones are cut short, as on the module, whose main loop does not
    finish them in time either. With the caps lifted the code renders byte
    for byte as upstream.
  Byte for byte as before in the host harness, for every Resonator and Texture
  test script, but for Texture Stretch's torture script, where Size and Pitch
  sweep fast enough that a capped search picks other windows (same levels).
- Upstream undefined behaviour made explicit, with ARM's results (the module's
  and the NTS-3's), so the host and web builds now match the device:
  clouds/resources.cc repeats the last entry of lut_xfade_in and lut_xfade_out,
  which Interpolate(table, 1.0f, 16.0f) reads (weighted by 0) at a dry/wet of
  exactly 1, one past upstream's end (granular_processor.cc, and Texture's own
  crossfade); clouds/dsp/correlator.cc skips the shift by 32 of a
  word-aligned candidate, which ARM evaluates to 0 and the host and
  WebAssembly to the unshifted word.
