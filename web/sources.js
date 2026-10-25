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

// Four bars of neo-soul voicings: Dm9, G13sus (the same upper notes over G),
// Cmaj9, Am11. Supersaw pads bloom through a low-pass on each bar and pump on
// the beat like a sidechained pad, over a sine sub, with a quiet bell
// arpeggio through a ping-pong delay. Only node-to-node connections: audio
// extensions that wrap AudioNode.connect break connections to an AudioParam.
function buildChords(ctx) {
  const bar = 16 * SIXTEENTH;
  const beat = 4 * SIXTEENTH;
  const chords = [
    { root: 38, notes: [53, 57, 60, 64] }, // Dm9: F A C E over D
    { root: 43, notes: [53, 57, 60, 64] }, // G13sus: F A C E over G
    { root: 36, notes: [52, 55, 59, 62] }, // Cmaj9: E G B D over C
    { root: 45, notes: [55, 60, 62, 64] }, // Am11: G C D E over A
  ];

  // Pad bus: low-pass that opens over each bar, then a beat-synced pump.
  const filter = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 700, Q: 0.9 });
  const pump = new GainNode(ctx, { gain: 1 });
  filter.connect(pump).connect(ctx.destination);
  chords.forEach((_, i) => {
    const t = i * bar;
    filter.frequency.setValueAtTime(650, t);
    filter.frequency.exponentialRampToValueAtTime(3400, t + bar * 0.55);
    filter.frequency.exponentialRampToValueAtTime(1300, t + bar);
  });
  for (let b = 0; b < 16; b++) {
    const t = b * beat;
    pump.gain.setValueAtTime(0.42, t);
    pump.gain.setTargetAtTime(1, t + 0.02, 0.09);
  }

  chords.forEach(({ root, notes }, i) => {
    const t = i * bar;
    // Three detuned saws per note, spread across the stereo field.
    notes.forEach((note, k) => {
      [[-14, -0.75], [0, (k % 2 ? 0.2 : -0.2)], [14, 0.75]].forEach(([detune, pan]) => {
        const osc = new OscillatorNode(ctx, { type: 'sawtooth', frequency: midiToHz(note), detune });
        const amp = new GainNode(ctx, { gain: 0 });
        const panner = new StereoPannerNode(ctx, { pan });
        amp.gain.setValueAtTime(0, t);
        amp.gain.linearRampToValueAtTime(0.032, t + 0.35);
        amp.gain.setValueAtTime(0.032, t + bar - 0.1);
        amp.gain.exponentialRampToValueAtTime(0.0001, t + bar + 0.9);
        osc.connect(amp).connect(panner).connect(filter);
        osc.start(t);
        osc.stop(t + bar + 1);
      });
    });
    // Sine sub on the root, ducked with the pads.
    const sub = new OscillatorNode(ctx, { type: 'sine', frequency: midiToHz(root) });
    const subAmp = new GainNode(ctx, { gain: 0 });
    subAmp.gain.setValueAtTime(0, t);
    subAmp.gain.linearRampToValueAtTime(0.22, t + 0.06);
    subAmp.gain.setValueAtTime(0.22, t + bar - 0.08);
    subAmp.gain.linearRampToValueAtTime(0, t + bar);
    sub.connect(subAmp).connect(pump);
    sub.start(t);
    sub.stop(t + bar + 0.05);
  });

  // Ping-pong delay, dotted eighths, for the bells.
  const send = new GainNode(ctx, { gain: 1 });
  const left = new DelayNode(ctx, { delayTime: 3 * SIXTEENTH, maxDelayTime: 1 });
  const right = new DelayNode(ctx, { delayTime: 3 * SIXTEENTH, maxDelayTime: 1 });
  const feedLeft = new GainNode(ctx, { gain: 0.42 });
  const feedRight = new GainNode(ctx, { gain: 0.42 });
  const dark = new BiquadFilterNode(ctx, { type: 'lowpass', frequency: 3800 });
  const panLeft = new StereoPannerNode(ctx, { pan: -0.8 });
  const panRight = new StereoPannerNode(ctx, { pan: 0.8 });
  const wet = new GainNode(ctx, { gain: 0.5 });
  send.connect(dark).connect(left);
  left.connect(panLeft).connect(wet);
  left.connect(feedLeft).connect(right);
  right.connect(panRight).connect(wet);
  right.connect(feedRight).connect(left);
  wet.connect(ctx.destination);

  // Bell arpeggio on the upper chord tones, an octave up, on the off-beats.
  const pattern = [0, 2, 1, 3, 2, 0, 3, 1];
  chords.forEach(({ notes }, i) => {
    for (let step = 0; step < 8; step++) {
      if (step % 2 === 0 && step !== 6) continue;
      const t = i * bar + step * 2 * SIXTEENTH;
      const f = midiToHz(notes[pattern[step]] + 12);
      const bell = new GainNode(ctx, { gain: 1 });
      bell.connect(ctx.destination);
      bell.connect(send);
      for (const [ratio, level, decay] of [[1, 0.09, 0.8], [3.01, 0.025, 0.25], [6.98, 0.008, 0.08]]) {
        const osc = new OscillatorNode(ctx, { type: 'sine', frequency: f * ratio });
        const amp = new GainNode(ctx, { gain: 0 });
        envelope(ctx, amp, t, level, 0.003, decay);
        osc.connect(amp).connect(bell);
        osc.start(t);
        osc.stop(t + decay + 0.05);
      }
    }
  });
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
  { id: 'chords', label: 'Chords', hint: 'neo-soul pads, sub and bells', seconds: 64 * SIXTEENTH, tail: 2.5, build: buildChords },
  { id: 'beat', label: 'Beat', hint: 'two bars of drums', seconds: 32 * SIXTEENTH, tail: 0.6, build: buildBeat },
  { id: 'plucks', label: 'Plucks', hint: 'mallet notes', seconds: 32 * SIXTEENTH, tail: 1.0, build: buildPlucks },
  { id: 'noise', label: 'Noise', hint: 'slow noise swells', seconds: 8, tail: 0, build: buildNoise },
];

export async function renderSources(sampleRate) {
  const buffers = {};
  await Promise.all(SOURCES.map(async (s) => {
    buffers[s.id] = await renderLoop(sampleRate, s.seconds, s.tail, s.build);
  }));
  return buffers;
}
