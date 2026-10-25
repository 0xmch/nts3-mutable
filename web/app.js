// nts3-mutable web page: runs the NTS-3 units (compiled to WebAssembly) in an
// AudioWorklet, with an XY pad, knobs, test sources, meters and recording.
//
// URL parameters: ?unit=<id> picks the unit, ?p=<v0.v1...> its parameter values
// and ?src=<id> the input loop (kept up to date, so the address bar is always a
// link to the current sound); ?embed=1 drops the page chrome for iframes.

import { SOURCES, renderSource } from './sources.js';

const UNITS = [
  {
    id: 'resonator', title: 'Resonator', short: 'Resonator', module: 'Rings', source: 'chords', touchLabel: 'strum', ripples: true,
    blurb: 'A bank of tuned modal filters or digital strings that the input sets ringing. Six models, up to four voices.',
    how: 'X picks the note, Y the structure. Each touch strums a new voice, and so does dragging to a new semitone. With Silence as the input, a tap plucks the strings.',
  },
  {
    id: 'texture', title: 'Texture', short: 'Texture', module: 'Clouds · granular', source: 'chords', touchLabel: 'freeze',
    blurb: 'Plays overlapping grains taken from the last second of input.',
    how: 'Touch to freeze the buffer and drag to scrub through it; release to let new audio in. X is the position, Y the grain size. With Density at 50%, each tap plays one grain.',
  },
  {
    id: 'texture_lofi', title: 'Texture Lo-Fi', short: 'Lo-Fi', module: 'Clouds · lo-fi', source: 'chords', touchLabel: 'freeze',
    blurb: "Clouds' granular mode in its lo-fi quality: mono, 8-bit mu-law samples at 16 kHz, with about seven seconds of buffer.",
    how: 'Same controls as Texture: touch to freeze and scrub, X is the position, Y the grain size. Position reaches further back.',
  },
  {
    id: 'texture_stretch', title: 'Texture Stretch', short: 'Stretch', module: 'Clouds · stretch', source: 'chords', touchLabel: 'freeze',
    blurb: 'Time-stretches and pitch-shifts the last second of input with WSOLA (waveform-similarity overlap-add).',
    how: 'Touch to freeze, then drag X through the frozen second. Y sets the window size, Density adds diffusion, and Texture sweeps a low-pass or high-pass filter.',
  },
  {
    id: 'texture_loop', title: 'Texture Loop', short: 'Loop', module: 'Clouds · looping delay', source: 'chords', touchLabel: 'loop',
    blurb: 'A delay that turns into a looper, with a pitch shifter on its output.',
    how: 'X sets the delay time. Touch to capture a loop and keep it playing; Y sets its length and Pitch transposes it.',
  },
  {
    id: 'metamod', title: 'Meta Mod', short: 'Meta Mod', module: 'Warps', source: 'chords', touchLabel: 'touch',
    blurb: 'Cross-modulates the input with an oscillator: wavefolding, ring modulation, XOR, comparator and a 20-band vocoder.',
    how: 'X morphs through the algorithms (the vocoder sits at the top), Y sets their timbre. Note tunes the oscillator; Carrier set to Input L uses the left input as the carrier and the right as the modulator.',
  },
  {
    id: 'freq_shifter', title: 'Freq Shifter', short: 'Shifter', module: 'Warps · easter egg', source: 'chords', touchLabel: 'touch',
    blurb: "Warps' hidden frequency shifter: every partial moves by the same number of hertz, up on the left and down on the right.",
    how: 'X sets the shift; the center is no shift, and either side sweeps to about 2 kHz up or down. Y feeds the output back for barber-pole sweeps.',
  },
];

const ASSIGN = { NONE: 0, X: 1, Y: 2, DEPTH: 3 };
const PHASE = { BEGAN: 0, MOVED: 1, ENDED: 2 };
const TYPE = {
  NONE: 0, PERCENT: 1, DB: 2, CENTS: 3, SEMI: 4, OCT: 5, HZ: 6, KHZ: 7, BPM: 8, MSEC: 9, SEC: 10,
  ENUM: 11, STRINGS: 12, DRYWET: 14, PAN: 15, SPREAD: 16, ONOFF: 17, MIDI_NOTE: 18,
};
const NOTE_NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('embed');

const $ = (id) => document.getElementById(id);
const clamp = (x, lo, hi) => Math.min(hi, Math.max(lo, x));

// ---- unit metadata -----------------------------------------------------------

const wasmCache = new Map();
const describeCache = new Map();

async function unitBytes(id) {
  if (!wasmCache.has(id)) {
    const response = await fetch(`units/${id}.wasm`);
    if (!response.ok) throw new Error(`units/${id}.wasm: ${response.status}`);
    wasmCache.set(id, await response.arrayBuffer());
  }
  return wasmCache.get(id);
}

// The unit describes itself (header parameters and pad mappings). Read on the
// main thread too, so the controls exist before audio starts.
async function describeUnit(id) {
  if (!describeCache.has(id)) {
    const { instance } = await WebAssembly.instantiate(await unitBytes(id), {});
    const e = instance.exports;
    if (e._initialize) e._initialize();
    const read = (ptr) => {
      const bytes = new Uint8Array(e.memory.buffer, ptr);
      let s = '';
      for (let i = 0; bytes[i]; i++) s += String.fromCharCode(bytes[i]);
      return s;
    };
    const describe = JSON.parse(read(e.nts3_describe()));
    // Parameters with too many strings to list ask the unit for each one.
    describe.params.forEach((p, i) => {
      if (p.dynamic) p.text = (v) => read(e.nts3_param_str(i, v));
    });
    describeCache.set(id, describe);
  }
  return describeCache.get(id);
}

function formatValue(p, v) {
  const decimal = p.frac_mode === 1;
  const scale = p.frac ? (decimal ? 10 ** p.frac : 2 ** p.frac) : 1;
  const num = (x) => (p.frac ? (x / scale).toFixed(decimal ? p.frac : 2) : String(x));
  const signed = (x) => (x > 0 ? '+' : x < 0 ? '−' : '') + num(Math.abs(x));
  switch (p.type) {
    case TYPE.PERCENT: return `${num(v)}%`;
    case TYPE.DB: return `${signed(v)} dB`;
    case TYPE.CENTS: return `${signed(v)} c`;
    case TYPE.SEMI: return `${signed(v)} st`;
    case TYPE.OCT: return `${signed(v)} oct`;
    case TYPE.HZ: return `${num(v)} Hz`;
    case TYPE.KHZ: return `${num(v)} kHz`;
    case TYPE.BPM: return `${num(v)} bpm`;
    case TYPE.MSEC: return `${num(v)} ms`;
    case TYPE.SEC: return `${num(v)} s`;
    case TYPE.ENUM: return String(p.min === 0 ? v + 1 : v);
    case TYPE.STRINGS: return p.text ? p.text(v) : (p.strings && p.strings[v - p.min]) || String(v);
    case TYPE.DRYWET: return v < 0 ? `D ${num(-v)}` : v > 0 ? `W ${num(v)}` : 'BAL';
    case TYPE.PAN: return v < 0 ? `L ${num(-v)}%` : v > 0 ? `R ${num(v)}%` : 'C';
    case TYPE.SPREAD: return v < 0 ? `< ${num(-v)}` : v > 0 ? `> ${num(v)}` : '0';
    case TYPE.ONOFF: return v ? 'on' : 'off';
    case TYPE.MIDI_NOTE: return `${NOTE_NAMES[((v % 12) + 12) % 12]}${Math.floor(v / 12) - 1}`;
    default: return num(v);
  }
}

// Pad curves from the logue SDK websim (applyCurveToParameter0to1), on the
// device's 10-bit pad resolution.
function applyCurve(x, curve, bipolar) {
  const v = Math.round(clamp(x, 0, 1) * 1023);
  const LOGMIN = -1.0986122886681098;
  const LOGMAX = 1.097743856134156;
  switch (curve) {
    case 0: // linear
      if (!bipolar) return x;
      return (v <= 500 ? (v / 500) * 512 : v >= 523 ? 512 + ((v - 523) / 500) * 511 : 512) / 1023;
    case 1: { // exp
      if (!bipolar) return (3 * Math.exp(LOGMIN + ((LOGMAX - LOGMIN) / 1023) * v) - 1) * 128 / 1023;
      if (v > 512) {
        const e = (3 * Math.exp(LOGMIN + ((LOGMAX - LOGMIN) / 510) * (v - 513)) - 1) * 128;
        return (512 + (511 * e) / 1023) / 1023;
      }
      const e = (3 * Math.exp(LOGMAX + ((LOGMIN - LOGMAX) / 512) * v) - 1) * 128;
      return (512 - (512 * e) / 1023) / 1023;
    }
    case 2: { // log
      if (!bipolar) {
        const l = LOGMAX - ((LOGMAX - LOGMIN) / 1023) * v;
        return (7.9921875 - (3 * Math.exp(l) - 1)) * 128 / 1023;
      }
      if (v > 512) {
        let l = LOGMAX - ((LOGMAX - LOGMIN) / 512) * (v - 512);
        l = 7.9921875 - (3 * Math.exp(l) - 1);
        return (4 + (3.9921875 * l) / 7.9921875) * 128 / 1023;
      }
      let l = LOGMIN + ((LOGMAX - LOGMIN) / 512) * v;
      l = -(3 * Math.exp(l) - 1);
      return (-(4 * l) / 7.9921875) * 128 / 1023;
    }
    case 3: // toggle
      return bipolar ? (v > 768 ? 1 : v > 256 ? 0.5 : 0) : v > 512 ? 1 : 0;
    case 4: // min clip
      if (!bipolar) return v > 512 ? (2 * (v - 512)) / 1023 : 0;
      return v > 768 ? 0.5 + (v - 768) / 510 : v > 256 ? 0.5 : v / 512;
    case 5: // max clip
      if (!bipolar) return v < 512 ? v / 512 : 1;
      return v > 768 ? 1 : v > 256 ? (v - 256) / 512 : 0;
    default:
      return x;
  }
}

const mappedValue = (map, x) => Math.round(map.min + applyCurve(x, map.curve, map.bipolar) * (map.max - map.min));
const unmapLinear = (map, value) => (map.max === map.min ? 0.5 : clamp((value - map.min) / (map.max - map.min), 0, 1));

// ---- state -------------------------------------------------------------------

const state = {
  unit: null,          // entry of UNITS
  describe: null,
  values: [],
  saved: new Map(),    // per-unit parameter values, kept across tab switches
  pad: { x: 0.5, y: 0.5, touching: false, pointer: false, hold: false },
  depth: 0.5,
  ctx: null,
  node: null,
  out: null,
  analyser: null,
  input: null,
  source: { id: null, node: null, stream: null },
  file: null,
  startSource: [...SOURCES.map((s) => s.id), 'silence'].includes(params.get('src')) ? params.get('src') : null,
  token: 0,
  recording: null,
};

function post(msg, transfer) {
  if (state.node) state.node.port.postMessage(msg, transfer || []);
}

function setParam(id, value, { fromPad = false } = {}) {
  const p = state.describe.params[id];
  value = clamp(Math.round(value), p.min, p.max);
  if (state.values[id] === value) return;
  state.values[id] = value;
  post({ type: 'param', id, value });
  updateControl(id);
  if (!fromPad) {
    // A knob on a pad axis or the depth slider moves that control with it.
    if (p.map.assign === ASSIGN.X) state.pad.x = unmapLinear(p.map, value);
    if (p.map.assign === ASSIGN.Y) state.pad.y = unmapLinear(p.map, value);
    if (p.map.assign === ASSIGN.DEPTH) {
      state.depth = unmapLinear(p.map, value);
      $('depth').value = state.depth;
      $('depth').style.setProperty('--fill', `${state.depth * 100}%`);
      $('depth-value').textContent = formatValue(p, value);
    }
    updateAxes();
  }
  saveUrl();
}

// The address bar mirrors the current sound: unit, parameter values, input.
let urlTimer = 0;
function saveUrl() {
  clearTimeout(urlTimer);
  urlTimer = setTimeout(() => {
    const url = new URL(location.href);
    url.searchParams.set('unit', state.unit.id);
    url.searchParams.set('p', state.values.join('.'));
    const src = state.source.id || state.startSource;
    if (src && src !== 'file' && src !== 'live') url.searchParams.set('src', src);
    history.replaceState(null, '', url);
    url.searchParams.delete('embed');
    $('fullpage').href = url.href;
  }, 250);
}

// Parameter values from ?p= for the unit named in ?unit=, if they fit it.
function urlValues(unitId, list) {
  if (params.get('unit') !== unitId || !params.get('p')) return null;
  const values = params.get('p').split('.').map(Number);
  if (values.length !== list.length || values.some((v) => !Number.isFinite(v))) return null;
  return values.map((v, i) => clamp(Math.round(v), list[i].min, list[i].max));
}

function applyPad() {
  const { params: list } = state.describe;
  list.forEach((p, id) => {
    const before = state.values[id];
    if (p.map.assign === ASSIGN.X) setParam(id, mappedValue(p.map, state.pad.x), { fromPad: true });
    if (p.map.assign === ASSIGN.Y) setParam(id, mappedValue(p.map, state.pad.y), { fromPad: true });
    // The resonator strums on each new semitone while touched: show it.
    if (state.unit.ripples && p.type === TYPE.MIDI_NOTE && state.pad.touching && state.values[id] !== before) {
      ripples.push({ x: state.pad.x, y: state.pad.y, t: performance.now() });
    }
  });
  updateAxes();
}

function applyDepth() {
  state.describe.params.forEach((p, id) => {
    if (p.map.assign === ASSIGN.DEPTH) setParam(id, mappedValue(p.map, state.depth), { fromPad: true });
  });
  const mapped = state.describe.params.findIndex((p) => p.map.assign === ASSIGN.DEPTH);
  $('depth-value').textContent = mapped >= 0 ? formatValue(state.describe.params[mapped], state.values[mapped]) : '';
  $('depth').style.setProperty('--fill', `${state.depth * 100}%`);
}

// ---- unit tabs and controls ------------------------------------------------

function buildTabs() {
  const nav = document.querySelector('.units');
  UNITS.forEach((u, i) => {
    const b = document.createElement('button');
    b.className = 'unit-tab';
    b.setAttribute('role', 'tab');
    b.dataset.unit = u.id;
    b.innerHTML = '<span class="t"></span><span class="s"><span class="long"></span><span class="short"></span></span>';
    b.querySelector('.t').textContent = u.short;
    b.querySelector('.long').textContent = u.module;
    b.querySelector('.short').textContent = u.module.split(' · ')[0];
    b.title = `${u.title} (${i + 1})`;
    b.addEventListener('click', () => selectUnit(u.id));
    nav.appendChild(b);
  });
}

// Strings-type parameters with a few named values get chips; longer lists
// (and those the unit formats on demand) get a knob.
const isChoice = (p) => p.type === TYPE.STRINGS && p.strings && p.strings.length <= 8;

const KNOB_R = 25;
const KNOB_C = 2 * Math.PI * KNOB_R;
const SWEEP = 0.75 * KNOB_C;

function knobSvg() {
  return `<svg class="knob" viewBox="0 0 64 64" aria-hidden="true">
    <circle class="track" cx="32" cy="32" r="${KNOB_R}" stroke-dasharray="${SWEEP} ${KNOB_C}" transform="rotate(135 32 32)"/>
    <circle class="arc" cx="32" cy="32" r="${KNOB_R}" transform="rotate(135 32 32)"/>
    <circle class="cap" cx="32" cy="32" r="18"/>
    <line class="tick" x1="32" y1="32" x2="32" y2="18"/>
  </svg>`;
}

function buildControls() {
  const box = $('controls');
  box.textContent = '';
  state.describe.params.forEach((p, id) => {
    const cell = document.createElement('div');
    cell.dataset.id = id;
    const badge = { [ASSIGN.X]: ['x', 'X'], [ASSIGN.Y]: ['y', 'Y'], [ASSIGN.DEPTH]: ['depth', 'DEPTH'] }[p.map.assign];
    if (isChoice(p)) {
      cell.className = 'ctl select' + (p.strings.length > 3 ? ' wide' : '');
      cell.innerHTML = '<span class="name"></span><div class="chips" role="radiogroup"></div>';
      cell.querySelector('.name').textContent = p.name;
      cell.querySelector('.chips').setAttribute('aria-label', p.name);
      p.strings.forEach((s, k) => {
        const chip = document.createElement('button');
        chip.className = 'chip';
        chip.setAttribute('role', 'radio');
        chip.textContent = s;
        chip.addEventListener('click', () => setParam(id, p.min + k));
        cell.querySelector('.chips').appendChild(chip);
      });
    } else {
      cell.className = 'ctl' + (badge ? ` mapped map-${badge[0]}` : '');
      cell.innerHTML = `<span class="name"></span>${knobSvg()}<span class="value"></span>`;
      cell.querySelector('.name').textContent = p.name;
      const knob = cell.querySelector('.knob');
      knob.setAttribute('role', 'slider');
      knob.setAttribute('aria-label', p.name);
      knob.setAttribute('aria-valuemin', p.min);
      knob.setAttribute('aria-valuemax', p.max);
      knob.setAttribute('tabindex', '0');
      bindKnob(knob, id);
      if (badge) cell.title = `${p.name} is on the ${badge[1] === 'DEPTH' ? 'depth slider' : `pad's ${badge[1]} axis`}`;
    }
    if (badge) {
      const b = document.createElement('span');
      b.className = `badge ${badge[0]}`;
      b.textContent = badge[1];
      cell.querySelector('.name').prepend(b);
    }
    box.appendChild(cell);
    updateControl(id);
  });
}

function bindKnob(knob, id) {
  const p = () => state.describe.params[id];
  const step = () => Math.max(1, Math.round((p().max - p().min) / 100));
  let drag = null;
  knob.addEventListener('pointerdown', (e) => {
    knob.setPointerCapture(e.pointerId);
    drag = { y: e.clientY, value: state.values[id] };
    e.preventDefault();
  });
  knob.addEventListener('pointermove', (e) => {
    if (!drag) return;
    const range = p().max - p().min;
    const fine = e.shiftKey ? 0.2 : 1;
    setParam(id, drag.value + ((drag.y - e.clientY) / 180) * range * fine);
  });
  const end = () => { drag = null; };
  knob.addEventListener('pointerup', end);
  knob.addEventListener('pointercancel', end);
  knob.addEventListener('dblclick', () => setParam(id, p().init));
  knob.addEventListener('wheel', (e) => {
    e.preventDefault();
    setParam(id, state.values[id] - Math.sign(e.deltaY) * step());
  }, { passive: false });
  knob.addEventListener('keydown', (e) => {
    const big = step() * 10;
    const moves = { ArrowUp: step(), ArrowRight: step(), ArrowDown: -step(), ArrowLeft: -step(), PageUp: big, PageDown: -big };
    if (e.key in moves) setParam(id, state.values[id] + moves[e.key]);
    else if (e.key === 'Home') setParam(id, p().min);
    else if (e.key === 'End') setParam(id, p().max);
    else return;
    e.preventDefault();
  });
}

function updateControl(id) {
  const cell = $('controls').querySelector(`[data-id="${id}"]`);
  if (!cell) return;
  const p = state.describe.params[id];
  const v = state.values[id];
  if (isChoice(p)) {
    cell.querySelectorAll('.chip').forEach((chip, k) => chip.setAttribute('aria-checked', String(p.min + k === v)));
    return;
  }
  const norm = (v - p.min) / (p.max - p.min || 1);
  // Bipolar ranges draw from zero, unipolar ones from the minimum.
  const zero = p.min < 0 && p.max > 0 ? -p.min / (p.max - p.min) : 0;
  const lo = Math.min(zero, norm);
  const hi = Math.max(zero, norm);
  const arc = cell.querySelector('.arc');
  arc.setAttribute('stroke-dasharray', `${Math.max(0.001, (hi - lo) * SWEEP)} ${KNOB_C}`);
  arc.setAttribute('stroke-dashoffset', String(-lo * SWEEP));
  cell.querySelector('.tick').setAttribute('transform', `rotate(${-135 + 270 * norm} 32 32)`);
  const text = formatValue(p, v);
  cell.querySelector('.value').textContent = text;
  const knob = cell.querySelector('.knob');
  knob.setAttribute('aria-valuenow', v);
  knob.setAttribute('aria-valuetext', text);
}

function updateAxes() {
  const { params: list } = state.describe;
  const label = (assign) => {
    const id = list.findIndex((p) => p.map.assign === assign);
    return id < 0 ? '' : `${list[id].name} <b>${formatValue(list[id], state.values[id])}</b>`;
  };
  $('axis-x').innerHTML = `<span class="k">X</span>${label(ASSIGN.X)}`;
  $('axis-y').innerHTML = `<span class="k">Y</span>${label(ASSIGN.Y)}`;
}

async function selectUnit(id) {
  const unit = UNITS.find((u) => u.id === id) || UNITS[0];
  if (state.describe && state.unit) state.saved.set(state.unit.id, { values: state.values.slice(), pad: { ...state.pad }, depth: state.depth });
  if (state.pad.touching) touch(PHASE.ENDED);
  state.unit = unit;
  document.querySelectorAll('.unit-tab').forEach((t) => t.setAttribute('aria-selected', String(t.dataset.unit === unit.id)));

  state.describe = await describeUnit(unit.id);
  const saved = state.saved.get(unit.id);
  const list = state.describe.params;
  const fromUrl = saved ? null : urlValues(unit.id, list);
  state.values = saved ? saved.values.slice()
    : fromUrl || list.map((p) => (p.map.assign ? p.map.value : p.init));
  const padOf = (assign, fallback) => {
    const id = list.findIndex((q) => q.map.assign === assign);
    return id >= 0 ? unmapLinear(list[id].map, state.values[id]) : fallback;
  };
  state.pad.x = saved ? saved.pad.x : padOf(ASSIGN.X, 0.5);
  state.pad.y = saved ? saved.pad.y : padOf(ASSIGN.Y, 0.5);
  state.depth = saved ? saved.depth : padOf(ASSIGN.DEPTH, 0.5);
  $('depth').value = state.depth;

  $('unit-title').innerHTML = `${unit.title}<span>${unit.module}</span>`;
  $('unit-blurb').textContent = unit.blurb;
  $('unit-how').textContent = unit.how;
  document.title = `${unit.title} · nts3-mutable`;
  buildControls();
  updateAxes();
  applyDepth();
  updatePadState();
  $('pad').classList.toggle('ruler', !!noteRuler());
  layout();
  saveUrl();
  if (state.ctx) await loadUnitAudio();
}

async function loadUnitAudio() {
  const token = ++state.token;
  const bytes = (await unitBytes(state.unit.id)).slice(0);
  post({ type: 'load', token, bytes, params: state.values.map((v, id) => [id, v]) }, [bytes]);
}

// ---- audio -------------------------------------------------------------------

async function startAudio() {
  if (state.ctx || state.starting) return;
  state.starting = true;
  const button = $('start-button');
  button.disabled = true;
  button.textContent = 'Starting…';
  try {
    const ctx = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' });
    await ctx.audioWorklet.addModule('worklet.js');
    const node = new AudioWorkletNode(ctx, 'nts3-unit', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
      channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers',
    });
    const input = new GainNode(ctx, { gain: 1 });
    const out = new GainNode(ctx, { gain: 0 });
    const analyser = new AnalyserNode(ctx, { fftSize: 4096, smoothingTimeConstant: 0.75 });
    input.connect(node).connect(out).connect(ctx.destination);
    node.connect(analyser);
    node.port.onmessage = (e) => onWorkletMessage(e.data);
    Object.assign(state, { ctx, node, input, out, analyser });
    applyVolume();
    await loadUnitAudio();
    await ctx.resume();
    await selectSource(state.startSource || state.unit.source);
    // The other loops render in the background.
    SOURCES.forEach((s) => renderSource(ctx.sampleRate, s.id));
    $('start').hidden = true;
    $('record').disabled = false;
    updatePower();
    ctx.addEventListener('statechange', updatePower);
  } catch (err) {
    button.disabled = false;
    button.textContent = 'Start audio';
    toast(`Audio could not start: ${err.message || err}`);
  }
  state.starting = false;
}

function updatePower() {
  const running = state.ctx && state.ctx.state === 'running';
  $('power').setAttribute('aria-pressed', String(!!running));
  $('power-label').textContent = !state.ctx ? 'Start' : running ? `${state.ctx.sampleRate / 1000} kHz` : 'Paused';
}

async function togglePower() {
  if (!state.ctx) return startAudio();
  if (state.ctx.state === 'running') await state.ctx.suspend();
  else await state.ctx.resume();
}

function onWorkletMessage(msg) {
  if (msg.type === 'meter') meter(msg.l, msg.r);
  else if (msg.type === 'audio' && state.recording) {
    state.recording.l.push(msg.l);
    state.recording.r.push(msg.r);
    state.recording.frames += msg.l.length;
    if (state.recording.frames > state.ctx.sampleRate * 300) toggleRecord();
  } else if (msg.type === 'error') toast(`Unit error: ${msg.message}`);
}

function stopSource() {
  const s = state.source;
  if (s.node) {
    try { s.node.stop && s.node.stop(); } catch (_) { /* already stopped */ }
    s.node.disconnect();
  }
  if (s.stream) s.stream.getTracks().forEach((t) => t.stop());
  state.source = { id: null, node: null, stream: null };
}

async function selectSource(id) {
  if (!state.ctx) return;
  if (id === 'file' && !state.file) { $('file').click(); return; }
  stopSource();
  let note = '';
  if (id === 'live') {
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false },
      });
      const node = new MediaStreamAudioSourceNode(state.ctx, { mediaStream: stream });
      node.connect(state.input);
      state.source = { id, node, stream };
      note = 'Live input. Use headphones to avoid feedback.';
    } catch (err) {
      toast('No access to an audio input.');
      return selectSource('silence');
    }
  } else if (id === 'silence') {
    state.source = { id, node: null, stream: null };
    note = state.unit.id === 'resonator' ? 'Silence: tap the pad to pluck.' : 'No input.';
  } else {
    const buffer = id === 'file' ? state.file.buffer : await renderSource(state.ctx.sampleRate, id);
    const node = new AudioBufferSourceNode(state.ctx, { buffer, loop: true });
    node.connect(state.input);
    node.start();
    state.source = { id, node, stream: null };
    note = id === 'file' ? state.file.name : SOURCES.find((s) => s.id === id).hint;
  }
  document.querySelectorAll('#sources .chip').forEach((c) => c.setAttribute('aria-checked', String(c.dataset.source === id)));
  if (id === 'live' || id === 'silence' || id === 'file') toast(note);
  saveUrl();
}

async function loadFile(file) {
  if (!file) return;
  if (!state.ctx) await startAudio();
  try {
    const buffer = await state.ctx.decodeAudioData(await file.arrayBuffer());
    state.file = { name: file.name, buffer };
    const chip = document.querySelector('[data-source="file"]');
    chip.textContent = file.name.length > 16 ? `${file.name.slice(0, 14)}…` : file.name;
    chip.title = file.name;
    selectSource('file');
  } catch (_) {
    toast(`Could not decode ${file.name}.`);
  }
}

// Fade the end of the input row while more chips are hidden past it.
function fadeSources() {
  const box = $('sources');
  box.classList.toggle('more', box.scrollLeft + box.clientWidth < box.scrollWidth - 2);
}

function buildSources() {
  const box = $('sources');
  box.addEventListener('scroll', fadeSources, { passive: true });
  const entries = [
    ...SOURCES.map((s) => [s.id, s.label, s.hint]),
    ['silence', 'Silence', 'no input'],
    ['file', 'File…', 'your own audio file; you can also drop one on the page'],
    ['live', 'Live', 'microphone or line input'],
  ];
  for (const [id, label, hint] of entries) {
    const chip = document.createElement('button');
    chip.className = 'chip';
    chip.setAttribute('role', 'radio');
    chip.dataset.source = id;
    chip.textContent = label;
    chip.title = hint;
    chip.addEventListener('click', () => (id === 'file' ? $('file').click() : selectSource(id)));
    box.appendChild(chip);
  }
  $('file').addEventListener('change', (e) => loadFile(e.target.files[0]));
  addEventListener('dragover', (e) => { e.preventDefault(); box.classList.add('drop'); });
  addEventListener('dragleave', (e) => { if (!e.relatedTarget) box.classList.remove('drop'); });
  addEventListener('drop', (e) => {
    e.preventDefault();
    box.classList.remove('drop');
    loadFile(e.dataTransfer.files[0]);
  });
}

// Output volume: 0..1 on a 60 dB scale; 0.7 is -18 dB.
function applyVolume() {
  const s = Number($('volume').value);
  const db = -60 + 60 * s;
  $('volume-value').textContent = s === 0 ? 'mute' : `${Math.round(db)} dB`;
  $('volume').style.setProperty('--fill', `${s * 100}%`);
  if (state.out) state.out.gain.setTargetAtTime(s === 0 ? 0 : 10 ** (db / 20), state.ctx.currentTime, 0.02);
}

const meterHold = { l: 0, r: 0, lt: 0, rt: 0 };
function meter(l, r) {
  const pos = (x) => clamp((20 * Math.log10(Math.max(x, 1e-6)) + 54) / 54, 0, 1);
  const now = performance.now();
  for (const [k, v] of [['l', l], ['r', r]]) {
    const p = pos(v);
    $(`meter-${k}`).style.width = `${p * 100}%`;
    if (p >= meterHold[k] || now - meterHold[`${k}t`] > 1200) {
      meterHold[k] = p;
      meterHold[`${k}t`] = now;
    }
    $(`hold-${k}`).style.left = `calc(${meterHold[k] * 100}% - 2px)`;
  }
}

function toggleRecord() {
  const button = $('record');
  if (!state.recording) {
    state.recording = { l: [], r: [], frames: 0, started: performance.now() };
    post({ type: 'record', on: true });
    button.setAttribute('aria-pressed', 'true');
    const tick = () => {
      if (!state.recording) return;
      const s = Math.floor((performance.now() - state.recording.started) / 1000);
      button.textContent = `Rec ${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
      setTimeout(tick, 250);
    };
    tick();
    return;
  }
  post({ type: 'record', on: false });
  const take = state.recording;
  state.recording = null;
  button.setAttribute('aria-pressed', 'false');
  button.textContent = 'Rec';
  if (!take.frames) return;
  const blob = encodeWav(take, state.ctx.sampleRate);
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = `${state.unit.id}-${new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-')}.wav`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  toast(`Saved ${(take.frames / state.ctx.sampleRate).toFixed(1)} s as ${a.download}`);
}

// 16-bit stereo PCM WAV.
function encodeWav(take, sampleRate) {
  const view = new DataView(new ArrayBuffer(44 + take.frames * 4));
  const text = (o, s) => [...s].forEach((c, i) => view.setUint8(o + i, c.charCodeAt(0)));
  text(0, 'RIFF'); view.setUint32(4, 36 + take.frames * 4, true); text(8, 'WAVE');
  text(12, 'fmt '); view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, 2, true);
  view.setUint32(24, sampleRate, true); view.setUint32(28, sampleRate * 4, true); view.setUint16(32, 4, true); view.setUint16(34, 16, true);
  text(36, 'data'); view.setUint32(40, take.frames * 4, true);
  let o = 44;
  take.l.forEach((l, k) => {
    const r = take.r[k];
    for (let i = 0; i < l.length; i++, o += 4) {
      view.setInt16(o, clamp(Math.round(l[i] * 32767), -32768, 32767), true);
      view.setInt16(o + 2, clamp(Math.round(r[i] * 32767), -32768, 32767), true);
    }
  });
  return new Blob([view], { type: 'audio/wav' });
}

let toastTimer = 0;
function toast(text) {
  const t = $('toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 4000);
}

// ---- pad -------------------------------------------------------------------

const trail = [];
const ripples = [];

function touch(phase) {
  const x = Math.round(state.pad.x * 1023);
  const y = Math.round(state.pad.y * 1023);
  if (phase === PHASE.BEGAN) {
    state.pad.touching = true;
    ripples.push({ x: state.pad.x, y: state.pad.y, t: performance.now() });
  } else if (phase === PHASE.ENDED) {
    state.pad.touching = false;
  }
  post({ type: 'touch', phase, x, y });
  updatePadState();
}

function updatePadState() {
  const st = $('pad-state');
  const label = state.unit ? state.unit.touchLabel : 'touch';
  st.textContent = state.pad.touching ? (state.pad.hold ? `${label} · hold` : label) : state.pad.hold ? 'hold' : '';
  st.classList.toggle('on', state.pad.touching);
  $('pad').classList.toggle('active', state.pad.touching);
  $('pad').classList.toggle('held', state.pad.hold);
}

function moveTo(x, y) {
  state.pad.x = clamp(x, 0, 1);
  state.pad.y = clamp(y, 0, 1);
  applyPad();
  if (state.pad.touching) {
    post({ type: 'touch', phase: PHASE.MOVED, x: Math.round(state.pad.x * 1023), y: Math.round(state.pad.y * 1023) });
    trail.push({ x: state.pad.x, y: state.pad.y, t: performance.now() });
  }
}

function bindPad() {
  const pad = $('pad');
  const at = (e) => {
    const r = pad.getBoundingClientRect();
    return [(e.clientX - r.left) / r.width, 1 - (e.clientY - r.top) / r.height];
  };
  pad.addEventListener('pointerdown', (e) => {
    if (!state.ctx || e.target.closest('.start')) return;
    pad.setPointerCapture(e.pointerId);
    state.pad.pointer = true;
    moveTo(...at(e));
    if (!state.pad.touching) touch(PHASE.BEGAN);
    else if (state.pad.hold) ripples.push({ x: state.pad.x, y: state.pad.y, t: performance.now() });
  });
  pad.addEventListener('pointermove', (e) => {
    if (state.pad.pointer) moveTo(...at(e));
  });
  const up = () => {
    if (!state.pad.pointer) return;
    state.pad.pointer = false;
    if (!state.pad.hold) touch(PHASE.ENDED);
  };
  pad.addEventListener('pointerup', up);
  pad.addEventListener('pointercancel', up);
  pad.addEventListener('keydown', (e) => {
    if (!state.ctx) return;
    const d = e.shiftKey ? 0.1 : 0.02;
    const moves = { ArrowLeft: [-d, 0], ArrowRight: [d, 0], ArrowUp: [0, d], ArrowDown: [0, -d] };
    if (e.key in moves) {
      moveTo(state.pad.x + moves[e.key][0], state.pad.y + moves[e.key][1]);
    } else if (e.key === ' ' || e.key === 'Enter') {
      touch(state.pad.touching ? PHASE.ENDED : PHASE.BEGAN);
    } else return;
    e.preventDefault();
  });
  $('hold').addEventListener('click', toggleHold);
  $('bypass').addEventListener('click', toggleBypass);
  $('depth').addEventListener('input', (e) => { state.depth = Number(e.target.value); applyDepth(); });
}

function toggleHold() {
  state.pad.hold = !state.pad.hold;
  $('hold').setAttribute('aria-pressed', String(state.pad.hold));
  if (!state.pad.hold && state.pad.touching && !state.pad.pointer) touch(PHASE.ENDED);
  else updatePadState();
}

function toggleBypass() {
  const on = $('bypass').getAttribute('aria-pressed') !== 'true';
  $('bypass').setAttribute('aria-pressed', String(on));
  post({ type: 'bypass', on });
}

// ---- pad drawing -----------------------------------------------------------

// The X-mapped parameter when it is a note (Resonator): drawn as a keyboard
// ruler with octave lines.
function noteRuler() {
  if (!state.describe) return null;
  const p = state.describe.params.find((q) => q.map.assign === ASSIGN.X && q.type === TYPE.MIDI_NOTE);
  return p && p.map.curve === 0 && !p.map.bipolar ? p.map : null;
}

function drawRuler(W, H, dpr) {
  const map = noteRuler();
  if (!map) return;
  const lo = Math.min(map.min, map.max);
  const hi = Math.max(map.min, map.max);
  g.font = `${10 * dpr}px "Geist Mono", ui-monospace, monospace`;
  g.textBaseline = 'middle';
  for (let n = lo; n <= hi; n++) {
    const x = Math.round(((n - map.min) / (map.max - map.min)) * W) + 0.5;
    const black = [1, 3, 6, 8, 10].includes(((n % 12) + 12) % 12);
    if (n % 12 === 0) {
      g.strokeStyle = 'rgba(187,154,247,.16)';
      g.lineWidth = 1;
      g.beginPath(); g.moveTo(x, 22 * dpr); g.lineTo(x, H); g.stroke();
      if (x > 8 * dpr && x < W - 24 * dpr) {
        g.fillStyle = 'rgba(187,154,247,.75)';
        g.fillText(`C${Math.floor(n / 12) - 1}`, x + 4 * dpr, 12 * dpr);
      }
    }
    g.strokeStyle = black ? 'rgba(150,160,185,.18)' : 'rgba(200,211,245,.32)';
    g.lineWidth = 1;
    g.beginPath(); g.moveTo(x, 0); g.lineTo(x, (black ? 4 : 7) * dpr); g.stroke();
  }
}

const canvas = $('pad-canvas');
const g = canvas.getContext('2d');
let spectrum = null;
let freq = null;
let wave = null;

function resizeCanvas() {
  const r = canvas.getBoundingClientRect();
  const dpr = Math.min(devicePixelRatio || 1, 2);
  const w = Math.round(r.width * dpr);
  const h = Math.round(r.height * dpr);
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
  return dpr;
}

function draw(now) {
  const dpr = resizeCanvas();
  const W = canvas.width;
  const H = canvas.height;
  g.clearRect(0, 0, W, H);

  // grid
  g.lineWidth = 1;
  for (let i = 1; i < 8; i++) {
    g.strokeStyle = i === 4 ? 'rgba(150,160,185,.16)' : 'rgba(150,160,185,.07)';
    const x = Math.round((i * W) / 8) + 0.5;
    const y = Math.round((i * H) / 8) + 0.5;
    g.beginPath(); g.moveTo(x, 0); g.lineTo(x, H); g.stroke();
    g.beginPath(); g.moveTo(0, y); g.lineTo(W, y); g.stroke();
  }

  drawRuler(W, H, dpr);

  // output spectrum along the bottom, log frequency, 30 Hz to 16 kHz
  if (state.analyser) {
    const a = state.analyser;
    if (!freq) {
      freq = new Float32Array(a.frequencyBinCount);
      wave = new Float32Array(a.fftSize);
      spectrum = new Float32Array(160);
    }
    a.getFloatFrequencyData(freq);
    const binHz = state.ctx.sampleRate / a.fftSize;
    for (let i = 0; i < spectrum.length; i++) {
      const f = 30 * Math.pow(16000 / 30, i / (spectrum.length - 1));
      const db = freq[Math.min(freq.length - 1, Math.round(f / binHz))];
      const level = clamp((db + 96) / 84, 0, 1);
      spectrum[i] += (level - spectrum[i]) * 0.35;
    }
    const grad = g.createLinearGradient(0, H * 0.45, 0, H);
    grad.addColorStop(0, 'rgba(187,154,247,.0)');
    grad.addColorStop(1, 'rgba(187,154,247,.22)');
    g.beginPath();
    g.moveTo(0, H);
    for (let i = 0; i < spectrum.length; i++) g.lineTo((i / (spectrum.length - 1)) * W, H - spectrum[i] * H * 0.5);
    g.lineTo(W, H);
    g.closePath();
    g.fillStyle = grad;
    g.fill();
    g.beginPath();
    for (let i = 0; i < spectrum.length; i++) {
      const x = (i / (spectrum.length - 1)) * W;
      const y = H - spectrum[i] * H * 0.5;
      if (i) g.lineTo(x, y); else g.moveTo(x, y);
    }
    const line = g.createLinearGradient(0, 0, W, 0);
    line.addColorStop(0, 'rgba(122,162,247,.7)');
    line.addColorStop(0.55, 'rgba(187,154,247,.7)');
    line.addColorStop(1, 'rgba(125,207,255,.7)');
    g.strokeStyle = line;
    g.lineWidth = 1.5 * dpr;
    g.stroke();

    // faint oscilloscope through the middle
    a.getFloatTimeDomainData(wave);
    g.beginPath();
    const n = 512;
    for (let i = 0; i < n; i++) {
      const x = (i / (n - 1)) * W;
      const y = H * 0.5 - wave[i * 2] * H * 0.18;
      if (i) g.lineTo(x, y); else g.moveTo(x, y);
    }
    g.strokeStyle = 'rgba(125,207,255,.16)';
    g.lineWidth = 1 * dpr;
    g.stroke();
  }

  const px = (x) => x * W;
  const py = (y) => (1 - y) * H;

  // trail
  if (!reducedMotion) {
    while (trail.length && now - trail[0].t > 700) trail.shift();
    for (let i = 1; i < trail.length; i++) {
      const age = (now - trail[i].t) / 700;
      g.strokeStyle = `rgba(125,207,255,${0.55 * (1 - age)})`;
      g.lineWidth = (1 + 5 * (1 - age)) * dpr;
      g.lineCap = 'round';
      g.beginPath();
      g.moveTo(px(trail[i - 1].x), py(trail[i - 1].y));
      g.lineTo(px(trail[i].x), py(trail[i].y));
      g.stroke();
    }
    while (ripples.length && now - ripples[0].t > 700) ripples.shift();
    for (const r of ripples) {
      const age = (now - r.t) / 700;
      g.strokeStyle = `rgba(187,154,247,${0.6 * (1 - age)})`;
      g.lineWidth = 2 * dpr;
      g.beginPath();
      g.arc(px(r.x), py(r.y), (10 + 70 * age) * dpr, 0, Math.PI * 2);
      g.stroke();
    }
  }

  // cursor
  const cx = px(state.pad.x);
  const cy = py(state.pad.y);
  if (state.pad.touching) {
    g.setLineDash([4 * dpr, 6 * dpr]);
    g.strokeStyle = 'rgba(125,207,255,.28)';
    g.lineWidth = 1 * dpr;
    g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx, H); g.stroke();
    g.beginPath(); g.moveTo(cx, cy); g.lineTo(0, cy); g.stroke();
    g.setLineDash([]);
    const glow = g.createRadialGradient(cx, cy, 0, cx, cy, 46 * dpr);
    glow.addColorStop(0, 'rgba(125,207,255,.42)');
    glow.addColorStop(1, 'rgba(125,207,255,0)');
    g.fillStyle = glow;
    g.beginPath(); g.arc(cx, cy, 46 * dpr, 0, Math.PI * 2); g.fill();
    g.strokeStyle = state.pad.hold ? '#bb9af7' : '#7dcfff';
    g.lineWidth = 2 * dpr;
    g.beginPath(); g.arc(cx, cy, 15 * dpr, 0, Math.PI * 2); g.stroke();
    g.fillStyle = '#e8ebf2';
    g.beginPath(); g.arc(cx, cy, 4 * dpr, 0, Math.PI * 2); g.fill();
  } else {
    g.strokeStyle = 'rgba(200,211,245,.35)';
    g.lineWidth = 1.5 * dpr;
    g.beginPath(); g.arc(cx, cy, 12 * dpr, 0, Math.PI * 2); g.stroke();
    g.fillStyle = 'rgba(200,211,245,.35)';
    g.beginPath(); g.arc(cx, cy, 2.5 * dpr, 0, Math.PI * 2); g.fill();
  }
  requestAnimationFrame(draw);
}

// ---- layout ------------------------------------------------------------------

// The pad is the largest square that fits: beside the controls in landscape,
// above them (which then scroll on their own) in portrait.
function layout() {
  const shell = $('shell');
  const stacked = innerWidth < 720 || innerWidth / innerHeight < 1.1;
  shell.classList.toggle('stacked', stacked);
  const stage = $('stage');
  const rect = stage.getBoundingClientRect();
  const cs = getComputedStyle(stage);
  const padX = parseFloat(cs.paddingLeft) + parseFloat(cs.paddingRight);
  const padY = parseFloat(cs.paddingTop) + parseFloat(cs.paddingBottom);
  const gap = parseFloat(cs.columnGap) || 14;
  const tools = document.querySelector('.pad-tools').offsetHeight + 10;
  const size = stacked
    ? Math.min(rect.width - padX, (rect.height - padY) * 0.52)
    : Math.min(rect.height - padY - tools, (rect.width - padX - gap) * 0.6);
  shell.style.setProperty('--pad', `${Math.max(180, Math.floor(size))}px`);
  fitKnobs(stacked);
  fadeSources();
}

// In landscape, grow the knobs until the controls fill the side panel.
function fitKnobs(stacked) {
  const shell = $('shell');
  shell.style.removeProperty('--knob');
  if (stacked) return;
  const side = document.querySelector('.side');
  const knobs = [...$('controls').querySelectorAll('.knob')];
  if (!knobs.length) return;
  const cs = getComputedStyle(side);
  const room = side.clientHeight - parseFloat(cs.paddingTop) - parseFloat(cs.paddingBottom);
  const used = [...side.children].reduce((h, e) => h + e.offsetHeight, 0) + parseFloat(cs.rowGap) * (side.children.length - 1);
  const rows = new Set(knobs.map((k) => k.closest('.ctl').offsetTop)).size;
  const base = knobs[0].getBoundingClientRect().width;
  const cell = knobs[0].closest('.ctl').clientWidth - 24;
  const grown = Math.min(base + Math.max(0, room - used) / rows, cell, 104);
  if (grown > base + 2) shell.style.setProperty('--knob', `${Math.floor(grown)}px`);
}

// ---- boot --------------------------------------------------------------------

addEventListener('keydown', (e) => {
  if (e.target.closest('input, textarea, [role="slider"]') || e.metaKey || e.ctrlKey || e.altKey) return;
  const n = Number(e.key);
  if (n >= 1 && n <= UNITS.length) selectUnit(UNITS[n - 1].id);
  else if (e.key === 'h' || e.key === 'H') { if (state.ctx) toggleHold(); }
  else if (e.key === 'b' || e.key === 'B') { if (state.ctx) toggleBypass(); }
});
$('start-button').addEventListener('click', startAudio);
$('power').addEventListener('click', togglePower);
$('about-button').addEventListener('click', () => $('about').showModal());
addEventListener('resize', layout);
new ResizeObserver(layout).observe($('shell'));
layout();
$('volume').addEventListener('input', applyVolume);
$('record').addEventListener('click', toggleRecord);
$('record').disabled = true;

buildTabs();
buildSources();
bindPad();
applyVolume();
selectUnit(params.get('unit') || UNITS[0].id).catch((err) => toast(`Could not load the unit: ${err.message}`));
requestAnimationFrame(draw);
