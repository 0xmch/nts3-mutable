# nts3-mutable

Mutable Instruments DSP running as user effects on the KORG NTS-3 kaoss pad kit.
The MI code (third_party/eurorack, MIT) runs unchanged; each unit adds the
front-panel glue for the pad.

## Units

### Resonator (Rings)

| Control | Parameter |
|---|---|
| Pad X | Note, C2 to C6 in semitones |
| Pad Y | Structure |
| Depth | Mix (dry/wet) |
| Params | Brightness, Damping, Position, Model, Polyphony (1/2/4) |

Models: Modal, Sympath (sympathetic strings), String, FM Voice, Chords
(sympathetic strings tuned to the chord picked by Structure), String Rvb.

Touching the pad strums a new voice, and so does sliding to a new semitone.
With audio on the input, the input excites the resonator and its transients
strum too. With nothing on the input, the internal exciter plucks on touch, so
headphones are all you need. Rings is mono in, stereo (odd/even) out.

### Texture, Texture Lo-Fi, Texture Stretch, Texture Loop (Clouds)

One unit per Clouds playback mode: granular, stretch (WSOLA) and looping delay,
plus Texture Lo-Fi: granular in Clouds' mono 8-bit quality (mu-law samples, a
16 kHz internal rate, about 7 s of buffer).

| Control | Parameter |
|---|---|
| Pad X | Position |
| Pad Y | Size |
| Depth | Mix (dry/wet) |
| Params | Pitch (±24 semitones), Density, Texture, Feedback, Reverb |

Touching the pad freezes the buffer and fires Clouds' trigger; releasing it
unfreezes. In granular mode, Density at noon means no grains, so each tap
plays one. Outside granular mode, Density sets the diffusion and Texture the
filter, as on the module.

Clouds runs at its native 32 kHz between 48/32 kHz resamplers (flat to 13 kHz,
86 dB down from 17 kHz, about 2.6 ms of latency on the wet signal only). The
dry signal stays at 48 kHz and passes untouched. Quality is 16-bit stereo with the
module's 1 s buffer (mono 8-bit for Texture Lo-Fi), stereo spread at 50%. The spectral mode
is not built: it does a whole FFT frame in one call, which a render callback
cannot absorb.

### Meta Mod and Freq Shifter (Warps)

Meta Mod is Warps: X morphs through its algorithms (cross-fade, cross-folding,
diode and digital ring modulation, XOR, comparator, then the 20-band vocoder
at the top), Y sets their timbre, Depth the dry/wet. The internal oscillator
is the carrier (Note, Carrier shape) and the input is the modulator; with
Carrier set to Input L, the left input is the carrier and the right input the
modulator. Mod Drive and Carrier Drive are Warps' level knobs.

Freq Shifter is Warps' easter egg: X shifts every partial by the same number
of hertz (center: none, up to about 2 kHz either way, on Warps' own curve),
Y is feedback, Depth the dry/wet, and the up and down sidebands come out on
the left and right.

Warps runs at its native 96 kHz between 2x resamplers, so its vocoder bands,
Hilbert filters and 6x oversampling filters work as designed.

## Build

ARM units build on a machine with the logue SDK and its pinned toolchain
(`~/src/logue-sdk`, `tools/gcc/get_gcc_10_3-2021_10_linux.sh`):

    scripts/build.sh            # all units, results in dist/
    scripts/build.sh resonator

`NTS3_BUILD_HOST` picks the SSH host (default `pc`).

Load `dist/*.nts3unit` with KORG KONTROL Editor: drag the file onto the user
effects list and press SEND.

## Web page

`web/` is a static page that plays the units in the browser: an XY pad with
the live output spectrum, the unit's knobs (built from its own header), test
loops synthesized in the page, your own file or live input, meters, and
recording to WAV. Each unit is compiled from the same sources to standalone
WebAssembly (`web/glue.cc`, about 60 KB) and runs in an AudioWorklet, so the
page needs no special server headers and works on any static host or in an
iframe.

    scripts/web.sh              # builds web/units/*.wasm, serves http://localhost:8791/

URL parameters: `unit` (resonator, texture, texture_stretch, texture_loop),
`p` (parameter values joined with dots), `src` (beat, chords, plucks, noise,
silence) and `embed=1` (no header or footer, for an iframe). The address bar
always holds the current sound, so it doubles as a preset link.

`host/webtest.mjs` drives the page in a muted headless Chrome: it starts audio,
plays the pad on every unit, reads the meters and saves screenshots.

## KORG's browser simulator

    scripts/sim.sh resonator    # builds with emscripten on the build host

serves http://localhost:8790/ (index of the built units): KORG's websim page
with an XY pad, sample loops and a test keyboard, running the unit's code.
The page starts at volume 0.05.

## Offline tests

`host/` builds a unit natively and drives it through the same `unit_*` entry
points as the firmware:

    cd host && make UNIT=resonator
    build/resonator/harness -i in.wav -s script.txt -l 8 -o out.wav

Input is 48 kHz WAV (16-bit or float). Script lines are
`<seconds> param <id> <value>` or `<seconds> touch <began|moved|ended>`.
The harness prints peak/RMS per second and the render time.
