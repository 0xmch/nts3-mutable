// AudioWorklet that runs one nts3-mutable unit compiled to WebAssembly
// (web/glue.cc). The main thread sends the module bytes, parameter values and
// pad touches; this side renders audio and reports meters and recordings.

// Unit headers are 7-bit ASCII, and AudioWorkletGlobalScope has no
// TextDecoder in some browsers.
const readCString = (memory, ptr) => {
  const bytes = new Uint8Array(memory.buffer, ptr);
  let s = '';
  for (let i = 0; bytes[i]; i++) s += String.fromCharCode(bytes[i]);
  return s;
};

class UnitProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.unit = null;
    this.bypass = false;
    this.recording = false;
    this.peak = [0, 0];
    this.meterFrames = 0;
    this.port.onmessage = (event) => this.onMessage(event.data);
  }

  async onMessage(msg) {
    switch (msg.type) {
      case 'load': {
        try {
          const { instance } = await WebAssembly.instantiate(msg.bytes, {});
          const e = instance.exports;
          if (e._initialize) e._initialize();
          if (!e.nts3_init()) throw new Error('unit init failed');
          const max = e.nts3_max_frames();
          const describe = JSON.parse(readCString(e.memory, e.nts3_describe()));
          for (const [id, value] of msg.params || []) e.nts3_set_param(id, value);
          // Memory never grows (built without growth), so the views stay valid.
          this.unit = {
            e,
            max,
            input: new Float32Array(e.memory.buffer, e.nts3_in(), 2 * max),
            output: new Float32Array(e.memory.buffer, e.nts3_out(), 2 * max),
          };
          this.port.postMessage({ type: 'ready', token: msg.token, describe });
        } catch (err) {
          this.port.postMessage({ type: 'error', token: msg.token, message: String(err) });
        }
        break;
      }
      case 'param':
        if (this.unit) this.unit.e.nts3_set_param(msg.id, msg.value);
        break;
      case 'touch':
        if (this.unit) this.unit.e.nts3_touch(msg.phase, msg.x, msg.y);
        break;
      case 'bypass':
        this.bypass = msg.on;
        break;
      case 'record':
        this.recording = msg.on;
        break;
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const outL = out[0];
    const outR = out[1] || out[0];
    const n = outL.length;
    const source = inputs[0] || [];
    const inL = source[0];
    const inR = source[1] || source[0];
    const unit = this.unit;

    if (!unit || this.bypass) {
      if (inL) {
        outL.set(inL);
        outR.set(inR);
      } else {
        outL.fill(0);
        outR.fill(0);
      }
    } else {
      for (let start = 0; start < n; start += unit.max) {
        const frames = Math.min(unit.max, n - start);
        const input = unit.input;
        for (let i = 0; i < frames; i++) {
          input[2 * i] = inL ? inL[start + i] : 0;
          input[2 * i + 1] = inR ? inR[start + i] : 0;
        }
        unit.e.nts3_process(frames);
        const output = unit.output;
        for (let i = 0; i < frames; i++) {
          outL[start + i] = output[2 * i];
          outR[start + i] = output[2 * i + 1];
        }
      }
    }

    for (let i = 0; i < n; i++) {
      const l = Math.abs(outL[i]);
      const r = Math.abs(outR[i]);
      if (l > this.peak[0]) this.peak[0] = l;
      if (r > this.peak[1]) this.peak[1] = r;
    }
    this.meterFrames += n;
    if (this.meterFrames >= 1536) {
      this.port.postMessage({ type: 'meter', l: this.peak[0], r: this.peak[1] });
      this.peak[0] = this.peak[1] = 0;
      this.meterFrames = 0;
    }
    if (this.recording) {
      const l = outL.slice();
      const r = outR.slice();
      this.port.postMessage({ type: 'audio', l, r }, [l.buffer, r.buffer]);
    }
    return true;
  }
}

registerProcessor('nts3-unit', UnitProcessor);
