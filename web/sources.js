// Test sources synthesized at load time with OfflineAudioContext, so the page
// ships no samples. Every loop is seamless: each one renders with a tail, and
// the tail is folded back onto the start.

const BPM = 100;
const SIXTEENTH = 60 / BPM / 4;

// Small deterministic PRNG, so the noise is the same on every load.
function mulberry32(seed) {
  return () => {
    seed |= 0;
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function noiseBuffer(ctx, seconds, seed) {
  const random = mulberry32(seed);
  const buffer = ctx.createBuffer(1, Math.ceil(seconds * ctx.sampleRate), ctx.sampleRate);
  const data = buffer.getChannelData(0);
  for (let i = 0; i < data.length; i++) data[i] = random() * 2 - 1;
  return buffer;
}

const midiToHz = (note) => 440 * Math.pow(2, (note - 69) / 12);

async function renderLoop(sampleRate, seconds, tail, build) {
  const length = Math.round(seconds * sampleRate);
  const tailLength = Math.round(tail * sampleRate);
  const ctx = new OfflineAudioContext(2, length + tailLength, sampleRate);
  build(ctx);
  const rendered = await ctx.startRendering();
  const loop = new AudioBuffer({ numberOfChannels: 2, length, sampleRate });
  let peak = 0;
  for (let c = 0; c < 2; c++) {
    const src = rendered.getChannelData(c);
    const dst = loop.getChannelData(c);
    for (let i = 0; i < length; i++) {
      dst[i] = src[i] + (i < tailLength ? src[length + i] : 0);
      peak = Math.max(peak, Math.abs(dst[i]));
    }
  }
  const gain = peak > 0 ? 0.7 / peak : 1; // -3 dBFS peaks
  for (let c = 0; c < 2; c++) {
    const dst = loop.getChannelData(c);
    for (let i = 0; i < length; i++) dst[i] *= gain;
  }
  return loop;
}

function envelope(ctx, node, t, level, attack, decay) {
  node.gain.setValueAtTime(0, t);
  node.gain.linearRampToValueAtTime(level, t + attack);
  node.gain.exponentialRampToValueAtTime(0.0001, t + attack + decay);
}

// Two bars of drums: kick, snare, closed and open hats.
function buildBeat(ctx) {
  const noise = noiseBuffer(ctx, 1, 7);
  const kick = '1000001010000000' + '1000001010100000';
  const snare = '0000100000001000' + '0000100000001001';
  const hats = '1010101010101010' + '1010101010101000';
  const open = '0000000000000000' + '0000000000000010';
  for (let step = 0; step < 32; step++) {
    const t = step * SIXTEENTH;
    if (kick[step] === '1') {
      const osc = new OscillatorNode(ctx, { type: 'sine' });
      const amp = new GainNode(ctx, { gain: 0 });
      osc.frequency.setValueAtTime(150, t);
      osc.frequency.exponentialRampToValueAtTime(46, t + 0.12);
      envelope(ctx, amp, t, 1, 0.002, 0.42);
      osc.connect(amp).connect(ctx.destination);
      osc.start(t);
      osc.stop(t + 0.5);
    }
    if (snare[step] === '1') {
      const ghost = step === 31;
      const src = new AudioBufferSourceNode(ctx, { buffer: noise });
      const band = new BiquadFilterNode(ctx, { type: 'bandpass', frequency: 1900, Q: 0.8 });
      const amp = new GainNode(ctx, { gain: 0 });
      envelope(ctx, amp, t, ghost ? 0.18 : 0.55, 0.001, 0.19);
      src.connect(band).connect(amp).connect(ctx.destination);
      src.start(t, (step * 0.037) % 0.5);
      src.stop(t + 0.25);
      const body = new OscillatorNode(ctx, { type: 'triangle', frequency: 185 });
      const bodyAmp = new GainNode(ctx, { gain: 0 });
      envelope(ctx, bodyAmp, t, ghost ? 0.1 : 0.4, 0.001, 0.09);
      body.connect(bodyAmp).connect(ctx.destination);
      body.start(t);
      body.stop(t + 0.15);
    }
    if (hats[step] === '1' || open[step] === '1') {
      const isOpen = open[step] === '1';
      const src = new AudioBufferSourceNode(ctx, { buffer: noise });
      const high = new BiquadFilterNode(ctx, { type: 'highpass', frequency: 7200 });
      const amp = new GainNode(ctx, { gain: 0 });
      const accent = step % 4 === 0 ? 0.32 : 0.2;
      envelope(ctx, amp, t, isOpen ? 0.28 : accent, 0.001, isOpen ? 0.32 : 0.045);
      const pan = new StereoPannerNode(ctx, { pan: 0.25 });
      src.connect(high).connect(amp).connect(pan).connect(ctx.destination);
      src.start(t, (step * 0.071) % 0.5);
      src.stop(t + 0.4);
    }
  }
}

// Sixteen bars of bright, airy 90s racing-game harmony. Each key plays the
// "royal road" progression (IVmaj9, V9sus, iii7, vi9), then the music
// modulates up a minor third: C, Eb, F#, A and back to C, the four keys of one
// diminished-seventh axis. At every key change the bass slides down a half
// step from vi into the new IV (A to Ab, C to B, ...): up a minor third, down
// a semitone. Voicings are open (fourths and fifths, thirds tolerated, never
// seconds) and voice-led, common tones held.
//
// Sound: a syncopated electric piano with auto-pan tremolo, a string pad with
// a light pump, a bouncing bass, bell sparkles through a ping-pong delay and a
// bright reverb. Only node-to-node connections and automation: audio
// extensions that wrap AudioNode.connect break connections to an AudioParam.
const CHORD_BPM = 118;
const CHORD_SIXTEENTH = 60 / CHORD_BPM / 4;
const QUALITIES = [
  { name: 'maj9', tones: [0, 2, 4, 7, 11], must: [4, 11] },   // IV
  { name: '9sus', tones: [0, 2, 5, 7, 10], must: [5, 10] },   // V
  { name: 'm7(11)', tones: [0, 3, 5, 7, 10], must: [3, 10] }, // iii
  { name: 'm9', tones: [0, 2, 3, 7, 10], must: [2, 3] },      // vi
];
const DEGREES = [5, 7, 4, 9];  // IV V iii vi, in semitones above the key
const KEYS = [0, 3, 6, 9];     // C Eb F# A

// Score of a 4-note voicing's spacing: fourths and fifths are free, thirds and
// tritones cost a little, wide gaps a little more; seconds are not allowed.
function spacingCost(notes) {
  let cost = 0;
  for (let i = 1; i < notes.length; i++) {
    const gap = notes[i] - notes[i - 1];
    if (gap < 3) return Infinity;
    if (gap === 5 || gap === 7) continue;
    cost += gap > 9 ? 2 : 1.2;
  }
  return cost;
}

function bestVoicing(root, quality, previous) {
  const candidates = [];
  for (let note = 52; note <= 82; note++) {
    if (quality.tones.includes((((note - root) % 12) + 12) % 12)) candidates.push(note);
  }
  let best = null;
  const n = candidates.length;
  for (let a = 0; a < n; a++) for (let b = a + 1; b < n; b++) for (let c = b + 1; c < n; c++) for (let d = c + 1; d < n; d++) {
    const notes = [candidates[a], candidates[b], candidates[c], candidates[d]];
    const pcs = notes.map((x) => (((x - root) % 12) + 12) % 12);
    if (!quality.must.every((m) => pcs.includes(m)) || new Set(pcs).size < 4) continue;
    const spacing = spacingCost(notes);
    if (spacing === Infinity) continue;
    const motion = previous ? notes.reduce((sum, x, i) => sum + Math.abs(x - previous[i]), 0) : 0;
    const register = Math.abs((notes[0] + notes[3]) / 2 - 67) * 0.15;
    const cost = motion + 2 * spacing + register;
    if (!best || cost < best.cost) best = { notes, cost };
  }
  return best.notes;
}

// Voice-leads the loop twice and keeps the second pass, so the first chord
// also follows smoothly from the last one.
function chordProgression() {
  let chords = [];
  for (let pass = 0; pass < 2; pass++) {
    let previous = chords.length ? chords[chords.length - 1].upper : null;
    let bass = chords.length ? chords[chords.length - 1].root : 41;
    chords = [];
    for (const key of KEYS) {
      DEGREES.forEach((degree, k) => {
        const pc = (key + degree) % 12;
        let root = 36 + pc;
        for (const r of [24 + pc, 36 + pc, 48 + pc]) if (r >= 34 && r <= 49 && Math.abs(r - bass) < Math.abs(root - bass)) root = r;
        const upper = bestVoicing(root, QUALITIES[k], previous);
        chords.push({ root, upper, quality: QUALITIES[k].name });
        previous = upper;
        bass = root;
      });
    }
  }
  return chords;
}

// Stereo reverb impulse: decaying noise that loses a little treble over time.
function reverbImpulse(ctx, seconds, seed) {
  const random = mulberry32(seed);
  const length = Math.round(seconds * ctx.sampleRate);
  const ir = ctx.createBuffer(2, length, ctx.sampleRate);
  for (let c = 0; c < 2; c++) {
    const data = ir.getChannelData(c);
    let lp = 0;
    for (let i = 0; i < length; i++) {
      const t = i / length;
      lp += (0.9 - 0.45 * t) * (random() * 2 - 1 - lp);
      data[i] = lp * Math.pow(1 - t, 1.8) * (i < 240 ? i / 240 : 1);
    }
  }
  return ir;
}

// Adds decaying sine partials (the shape of envelope(): 3 ms attack, then an
// exponential fall to -80 dB over `decay`) straight into a buffer. The piano,
// bass and sparkles are hundreds of short notes; computing them here instead
// of as audio nodes makes the loop render several times faster.
function addPartials(out, sampleRate, t, frequency, partials, gain) {
  const start = Math.round(t * sampleRate);
  const attack = Math.round(0.003 * sampleRate);
  for (const [ratio, level, decay] of partials) {
    const w = (2 * Math.PI * frequency * ratio) / sampleRate;
    if (w >= Math.PI) continue;
    const n = Math.min(out.length - start, attack + Math.round(decay * sampleRate));
    const fall = Math.pow(0.0001 / level, 1 / (decay * sampleRate));
    const c = Math.cos(w), s = Math.sin(w);
    let re = 1, im = 0, env = level * gain;
    for (let i = 0; i < n; i++) {
      const a = i < attack ? (i / attack) * level * gain : (env *= fall);
      out[start + i] += a * im;
      const r = re * c - im * s;
      im = re * s + im * c;
      re = r;
    }
  }
}

function buildChords(ctx) {
  const step = CHORD_SIXTEENTH;
  const bar = 16 * step;
  const chords = chordProgression();
  const sr = ctx.sampleRate;
  const length = ctx.length;

  // Reverb with a mono send, which halves the convolution work.
  const reverbSend = new GainNode(ctx, { gain: 1, channelCount: 1, channelCountMode: 'explicit' });
  const reverb = new ConvolverNode(ctx, { buffer: reverbImpulse(ctx, 2.8, 23) });
  const reverbOut = new GainNode(ctx, { gain: 0.3 });
  reverbSend.connect(reverb).connect(reverbOut).connect(ctx.destination);

  // String pad (audio nodes): low-pass, light pump on the beat.
  const padFilter = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 4200, Q: 0.5 });
  const pump = new GainNode(ctx, { gain: 1 });
  const padSend = new GainNode(ctx, { gain: 0.5 });
  padFilter.connect(pump).connect(ctx.destination);
  pump.connect(padSend).connect(reverbSend);
  for (let b = 0; b < chords.length * 4; b++) {
    const t = b * 4 * step;
    pump.gain.setValueAtTime(0.8, t);
    pump.gain.setTargetAtTime(1, t + 0.02, 0.09);
  }

  const EP = [[1, 0.055, 1.6], [2, 0.018, 0.7], [3, 0.006, 0.3], [7.1, 0.004, 0.06]];
  const BELL = [[1, 0.05, 0.9], [2, 0.015, 0.4], [3, 0.005, 0.18]];
  const BASS = [[1, 0.32, 0.42], [2, 0.08, 0.2]];
  const ep = new Float32Array(length);
  const bass = new Float32Array(length);
  const bells = new Float32Array(length);

  chords.forEach(({ root, upper }, i) => {
    const t0 = i * bar;
    upper.forEach((note, k) => {
      [['sawtooth', -7, -0.6, 0.016], ['sawtooth', 7, 0.6, 0.016], ['triangle', 0, k % 2 ? 0.3 : -0.3, 0.03]].forEach(([type, detune, pan, level]) => {
        const osc = new OscillatorNode(ctx, { type, frequency: midiToHz(note), detune });
        const amp = new GainNode(ctx, { gain: 0 });
        const panner = new StereoPannerNode(ctx, { pan });
        amp.gain.setValueAtTime(0, t0);
        amp.gain.linearRampToValueAtTime(level, t0 + 0.4);
        amp.gain.setValueAtTime(level, t0 + bar - 0.1);
        amp.gain.exponentialRampToValueAtTime(0.0001, t0 + bar + 1.0);
        osc.connect(amp).connect(panner).connect(padFilter);
        osc.start(t0);
        osc.stop(t0 + bar + 1.1);
      });
    });
    // Electric piano comp: on the one, the "and" of two, and the three.
    for (const [s16, velocity] of [[0, 1], [6, 0.7], [10, 0.55]]) {
      for (const note of upper) addPartials(ep, sr, t0 + s16 * step, midiToHz(note), EP, velocity);
    }
    // Bass: root, octave, root, fifth.
    for (const [s16, interval] of [[0, 0], [6, 12], [10, 0], [14, 7]]) {
      addPartials(bass, sr, t0 + s16 * step, midiToHz(root + interval), BASS, 1);
    }
    // Sparkles from the top of the voicing, an octave up.
    for (const [s16, v] of [[3, 3], [11, 2]]) {
      addPartials(bells, sr, t0 + s16 * step, midiToHz(upper[v] + 12), BELL, 1);
    }
  });

  // Electric piano to stereo with an auto-pan tremolo (equal-power pan law).
  const epBuffer = ctx.createBuffer(2, length, sr);
  const epL = epBuffer.getChannelData(0);
  const epR = epBuffer.getChannelData(1);
  const rate = (2 * Math.PI * (CHORD_BPM / 60) * 1.5) / sr;
  for (let i = 0; i < length; i++) {
    const angle = (0.45 * Math.sin(rate * i) + 1) * (Math.PI / 4);
    epL[i] = ep[i] * Math.cos(angle);
    epR[i] = ep[i] * Math.sin(angle);
  }
  const play = (buffer) => {
    const node = new AudioBufferSourceNode(ctx, { buffer });
    node.start(0);
    return node;
  };
  const epNode = play(epBuffer);
  const epSend = new GainNode(ctx, { gain: 0.35 });
  epNode.connect(ctx.destination);
  epNode.connect(epSend).connect(reverbSend);

  const mono = (data) => {
    const buffer = ctx.createBuffer(1, length, sr);
    buffer.copyToChannel(data, 0);
    return buffer;
  };
  play(mono(bass)).connect(ctx.destination);

  // Ping-pong delay, dotted eighths, for the sparkles.
  const left = new DelayNode(ctx, { delayTime: 3 * step, maxDelayTime: 1 });
  const right = new DelayNode(ctx, { delayTime: 3 * step, maxDelayTime: 1 });
  const feedLeft = new GainNode(ctx, { gain: 0.38 });
  const feedRight = new GainNode(ctx, { gain: 0.38 });
  const tone = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 6500 });
  const panLeft = new StereoPannerNode(ctx, { pan: -0.85 });
  const panRight = new StereoPannerNode(ctx, { pan: 0.85 });
  const wet = new GainNode(ctx, { gain: 0.45 });
  const bellNode = play(mono(bells));
  bellNode.connect(ctx.destination);
  bellNode.connect(tone).connect(left);
  left.connect(panLeft).connect(wet);
  left.connect(feedLeft).connect(right);
  right.connect(panRight).connect(wet);
  right.connect(feedRight).connect(left);
  wet.connect(ctx.destination);
  wet.connect(reverbSend);
}

// Two bars of mallet notes in A minor pentatonic.
function buildPlucks(ctx) {
  const notes = [69, 0, 72, 76, 0, 74, 72, 0, 69, 67, 0, 69, 72, 0, 76, 79];
  notes.forEach((note, i) => {
    if (!note) return;
    const t = i * 2 * SIXTEENTH;
    const f = midiToHz(note);
    const pan = new StereoPannerNode(ctx, { pan: ((i * 5) % 7) / 7 - 0.4 });
    pan.connect(ctx.destination);
    for (const [ratio, level, decay] of [[1, 0.5, 0.9], [4, 0.12, 0.18], [10, 0.03, 0.05]]) {
      const osc = new OscillatorNode(ctx, { type: 'sine', frequency: f * ratio });
      const amp = new GainNode(ctx, { gain: 0 });
      envelope(ctx, amp, t, level, 0.002, decay);
      osc.connect(amp).connect(pan);
      osc.start(t);
      osc.stop(t + decay + 0.1);
    }
  });
}

// Eight seconds of filtered noise swelling twice, a good resonator exciter.
// The swell is a gain curve rather than an LFO connected to the gain: audio
// extensions that wrap AudioNode.connect (Mimi, EQ add-ons) break connections
// to an AudioParam.
function buildNoise(ctx) {
  const src = new AudioBufferSourceNode(ctx, { buffer: noiseBuffer(ctx, 9, 11) });
  const color = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 2600, Q: 0.5 });
  const amp = new GainNode(ctx, { gain: 0 });
  const swell = new Float32Array(257);
  for (let i = 0; i < swell.length; i++) {
    swell[i] = 0.35 + 0.3 * Math.sin((2 * Math.PI * 2 * i) / (swell.length - 1));
  }
  amp.gain.setValueCurveAtTime(swell, 0, 8);
  src.connect(color).connect(amp).connect(ctx.destination);
  src.start(0);
}

export const SOURCES = [
  { id: 'chords', label: 'Chords', hint: 'bright 90s racing chords, up a minor third per key', seconds: 16 * 16 * CHORD_SIXTEENTH, tail: 3.6, build: buildChords },
  { id: 'beat', label: 'Beat', hint: 'two bars of drums', seconds: 32 * SIXTEENTH, tail: 0.6, build: buildBeat },
  { id: 'plucks', label: 'Plucks', hint: 'mallet notes', seconds: 32 * SIXTEENTH, tail: 1.0, build: buildPlucks },
  { id: 'noise', label: 'Noise', hint: 'slow noise swells', seconds: 8, tail: 0, build: buildNoise },
];

// Renders one source by id (cached), so the page can start on the selected
// loop and render the others afterwards.
const rendered = new Map();
export function renderSource(sampleRate, id) {
  const key = `${sampleRate}:${id}`;
  if (!rendered.has(key)) {
    const s = SOURCES.find((x) => x.id === id);
    rendered.set(key, renderLoop(sampleRate, s.seconds, s.tail, s.build));
  }
  return rendered.get(key);
}

export async function renderSources(sampleRate) {
  const buffers = {};
  await Promise.all(SOURCES.map(async (s) => { buffers[s.id] = await renderSource(sampleRate, s.id); }));
  return buffers;
}
