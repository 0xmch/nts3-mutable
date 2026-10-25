// End-to-end check of web/ in a muted headless Chrome over the DevTools
// protocol: starts audio, plays the pad, reads the meters for each unit and
// saves screenshots.
//
//   node host/webtest.mjs [url] [outdir]
// Default url http://localhost:8791/, screenshots go to outdir (default /tmp).

import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const url = process.argv[2] || 'http://localhost:8791/';
const outdir = process.argv[3] || tmpdir();
const port = 9333;
const chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const profile = mkdtempSync(join(tmpdir(), 'nts3-webtest-'));
const proc = spawn(chrome, [
  '--headless=new', '--mute-audio', '--no-first-run', '--no-default-browser-check',
  `--user-data-dir=${profile}`, `--remote-debugging-port=${port}`,
  '--autoplay-policy=no-user-gesture-required', '--window-size=1440,820', 'about:blank',
], { stdio: 'ignore' });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws;
let nextId = 1;
const pending = new Map();
const logs = [];

async function connect() {
  for (let i = 0; i < 50; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      const page = list.find((t) => t.type === 'page');
      if (page) return page.webSocketDebuggerUrl;
    } catch (_) { /* not up yet */ }
    await sleep(200);
  }
  throw new Error('Chrome did not start');
}

function send(method, params = {}) {
  const id = nextId++;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
}

const evaluate = async (expression) => {
  const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.text + ' ' + JSON.stringify(r.exceptionDetails.exception?.description));
  return r.result.value;
};

async function mouse(type, x, y) {
  await send('Input.dispatchMouseEvent', { type, x, y, button: 'left', buttons: type === 'mouseReleased' ? 0 : 1, clickCount: 1, pointerType: 'mouse' });
}

// Single characters are typed; named keys (ArrowUp...) are sent with their
// key code.
const KEY_CODES = { ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40 };
async function key(k) {
  const named = k.length > 1;
  const down = named ? { key: k, code: k, windowsVirtualKeyCode: KEY_CODES[k] } : { key: k, text: k };
  await send('Input.dispatchKeyEvent', { type: named ? 'rawKeyDown' : 'keyDown', ...down });
  await send('Input.dispatchKeyEvent', { type: 'keyUp', ...down });
}

async function screenshot(name) {
  const { data } = await send('Page.captureScreenshot', { format: 'png' });
  const path = join(outdir, `${name}.png`);
  writeFileSync(path, Buffer.from(data, 'base64'));
  return path;
}

// Page scroll check: an app page should fit its viewport.
const scroll = () => evaluate(`({
  page: document.documentElement.scrollHeight - innerHeight,
  wide: document.documentElement.scrollWidth - innerWidth,
  overflow: [...document.querySelectorAll('body *')].filter((e) => e.getBoundingClientRect().right > innerWidth + 1 && getComputedStyle(e).position !== 'fixed' && !e.closest('dialog, .sources'))
    .slice(0, 4).map((e) => (e.id || e.className || e.tagName) + ':' + Math.round(e.getBoundingClientRect().right)),
  side: (() => { const e = document.querySelector('.side'); return e.scrollHeight - e.clientHeight; })(),
  pad: getComputedStyle(document.getElementById('shell')).getPropertyValue('--pad'),
})`);

// Peak meter widths (percent of the -54..0 dB scale) over a short window.
const meters = () => evaluate(`(async () => {
  let peak = 0;
  for (let i = 0; i < 12; i++) {
    peak = Math.max(peak, parseFloat(document.getElementById('meter-l').style.width) || 0,
                          parseFloat(document.getElementById('meter-r').style.width) || 0);
    await new Promise((r) => setTimeout(r, 60));
  }
  return peak;
})()`);

async function main() {
  ws = new WebSocket(await connect());
  await new Promise((r) => ws.addEventListener('open', r));
  ws.addEventListener('message', (event) => {
    const msg = JSON.parse(event.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      msg.error ? reject(new Error(msg.error.message)) : resolve(msg.result);
    } else if (msg.method === 'Runtime.consoleAPICalled') {
      logs.push(`console.${msg.params.type}: ${msg.params.args.map((a) => a.value ?? a.description).join(' ')}`);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      logs.push(`exception: ${msg.params.exceptionDetails.exception?.description || msg.params.exceptionDetails.text}`);
    } else if (msg.method === 'Log.entryAdded') {
      logs.push(`log.${msg.params.entry.level}: ${msg.params.entry.text}`);
    }
  });
  await send('Runtime.enable');
  await send('Log.enable');
  await send('Page.enable');
  // Audio extensions (Mimi, EQ add-ons) wrap AudioNode.prototype.connect and
  // forward all three arguments. That changes how the browser resolves the
  // connect() overloads, so the page has to work under such a wrapper too.
  await send('Page.addScriptToEvaluateOnNewDocument', { source: `
    const original = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function (destination, output, input) {
      return original.call(this, destination, output, input);
    };` });
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 820, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: `${url}?unit=resonator` });
  await sleep(1500);
  const shots = [await screenshot('web-idle')];
  const desktopScroll = await scroll();

  const t0 = Date.now();
  await evaluate(`document.getElementById('start-button').click()`);
  // Starting renders the test loops; wait until the power button shows a rate.
  let startMs = -1;
  for (let i = 0; i < 100; i++) {
    if (/kHz/.test(await evaluate(`document.getElementById('power-label').textContent`))) { startMs = Date.now() - t0; break; }
    await sleep(100);
  }
  await sleep(1000);
  const results = {};
  results.started = await evaluate(`({ power: document.getElementById('power-label').textContent, toast: document.getElementById('toast').hidden ? '' : document.getElementById('toast').textContent })`);
  results.started.ms = startMs;
  const rect = await evaluate(`(() => { const r = document.getElementById('pad').getBoundingClientRect(); return {x: r.x, y: r.y, w: r.width, h: r.height}; })()`);
  const at = (fx, fy) => [rect.x + fx * rect.w, rect.y + (1 - fy) * rect.h];

  for (const [n, id] of [['1', 'resonator'], ['2', 'texture'], ['3', 'texture_stretch'], ['4', 'texture_loop']]) {
    await key(n);
    await sleep(1200);
    const before = await meters();
    // touch, drag across, release
    await mouse('mousePressed', ...at(0.2, 0.3));
    for (let i = 1; i <= 20; i++) {
      await mouse('mouseMoved', ...at(0.2 + 0.03 * i, 0.3 + 0.02 * i));
      await sleep(40);
    }
    const during = await meters();
    if (id === 'texture') shots.push(await screenshot('web-touch-texture'));
    if (id === 'resonator') shots.push(await screenshot('web-touch-resonator'));
    await mouse('mouseReleased', ...at(0.8, 0.7));
    await sleep(600);
    const after = await meters();
    const title = await evaluate(`document.getElementById('unit-title').textContent`);
    results[id] = { title, before, during, after };
  }

  // Resonator with no input: a tap must pluck.
  await key('1');
  await sleep(600);
  await evaluate(`document.querySelector('[data-source="silence"]').click()`);
  await sleep(2500);
  const quiet = await meters();
  await mouse('mousePressed', ...at(0.5, 0.25));
  await sleep(80);
  await mouse('mouseReleased', ...at(0.5, 0.25));
  const pluck = await meters();
  results.resonator_pluck = { silence: quiet, after_tap: pluck };

  // Reload after switching to a phone viewport, as a phone would load it.
  await send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 2, mobile: true });
  await send('Page.reload');
  await sleep(1500);
  results.scroll = { desktop: desktopScroll, phone: await scroll() };
  shots.push(await screenshot('web-phone'));

  // A shared link restores the sound: unit, parameters and input.
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 820, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: `${url}?unit=resonator&p=60.800.1000.700.700.300.1.2&src=noise` });
  await sleep(1500);
  results.link = await evaluate(`(() => {
    const v = [...document.querySelectorAll('#controls .ctl')].map((c) => c.querySelector('.value')?.textContent || c.querySelector('[aria-checked="true"]')?.textContent);
    return { values: v.join(' | '), axis: document.getElementById('axis-x').textContent };
  })()`);
  await evaluate(`document.querySelector('#controls [data-id="3"] .knob').focus()`);
  await key('ArrowUp');
  await sleep(500);
  results.link.after_knob = await evaluate(`new URL(location.href).search`);

  // Blog embed: an 800 px column, before audio starts.
  await send('Emulation.setDeviceMetricsOverride', { width: 800, height: 560, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: `${url}?unit=texture&embed=1` });
  await sleep(1500);
  results.scroll.embed = await scroll();
  shots.push(await screenshot('web-embed'));
  await send('Emulation.setDeviceMetricsOverride', { width: 1920, height: 1000, deviceScaleFactor: 1, mobile: false });
  await sleep(600);
  results.scroll.wide = await scroll();
  shots.push(await screenshot('web-wide'));

  console.log(JSON.stringify(results, null, 1));
  console.log(logs.length ? logs.join('\n') : 'no console errors or messages');
  console.log(shots.join('\n'));
}

main().catch((err) => { console.error(err); process.exitCode = 1; }).finally(() => { try { ws?.close(); } catch (_) {} proc.kill(); });
