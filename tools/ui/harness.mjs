// Drives the browser demo (ui/ with its mock bridge) in headless Chrome over
// the DevTools protocol. Shared by the browser tests (tools/ui/e2e.mjs) and
// the README media capture (tools/media/capture.mjs).
//
// No npm packages: Node 22 has fetch and WebSocket built in, and the demo is
// served by tools/serve-ui.py.

import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = process.env.NAMPLIFY_ROOT || resolve(fileURLToPath(new URL('../..', import.meta.url)));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function findChrome () {
  const candidates = [
    process.env.CHROME,
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/Applications/Chromium.app/Contents/MacOS/Chromium',
    '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable',
    '/usr/bin/chromium', '/usr/bin/chromium-browser',
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  ].filter(Boolean);
  const hit = candidates.find((p) => existsSync(p));
  if (!hit) throw new Error('Chrome not found; set CHROME=/path/to/chrome');
  return hit;
}

async function waitFor (url, ms = 15000) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    try { const r = await fetch(url); if (r.ok) return r; } catch {}
    await sleep(150);
  }
  throw new Error('Timed out waiting for ' + url);
}

/**
 * Starts the demo server and a headless Chrome, opens the demo and returns a
 * page handle. Always call close() afterwards.
 */
export async function openDemo ({ width = 1440, height = 900, uiPort = 8741, cdpPort = 9241 } = {}) {
  const server = spawn('python3', [join(ROOT, 'tools/serve-ui.py'), String(uiPort)], { stdio: 'ignore' });
  const profile = mkdtempSync(join(tmpdir(), 'namplify-ui-'));
  const chrome = spawn(findChrome(), [
    '--headless=new', `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--hide-scrollbars',
    '--force-color-profile=srgb', `--window-size=${width},${height}`,
    // Ubuntu runners restrict the user namespaces Chrome's sandbox needs.
    ...(process.env.CI ? ['--no-sandbox'] : []),
    'about:blank',
  ], { stdio: 'ignore' });

  const close = async () => {
    try { ws?.close(); } catch {}
    chrome.kill(); server.kill();
    await sleep(200);
    try { rmSync(profile, { recursive: true, force: true }); } catch {}
  };

  let ws;
  try {
    const url = `http://127.0.0.1:${uiPort}/index.html`;
    await waitFor(url);
    await waitFor(`http://127.0.0.1:${cdpPort}/json/version`);
    const target = (await (await fetch(`http://127.0.0.1:${cdpPort}/json/list`)).json())
      .find((t) => t.type === 'page');
    ws = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });

    let id = 0;
    const waiting = new Map();
    const errors = [];
    ws.onmessage = (m) => {
      const msg = JSON.parse(m.data);
      if (msg.id && waiting.has(msg.id)) { waiting.get(msg.id)(msg); waiting.delete(msg.id); return; }
      if (msg.method === 'Runtime.exceptionThrown')
        errors.push(msg.params.exceptionDetails.exception?.description || msg.params.exceptionDetails.text);
      if (msg.method === 'Log.entryAdded' && msg.params.entry.level === 'error')
        errors.push(msg.params.entry.text);
    };
    const send = (method, params = {}) => new Promise((res, rej) => {
      const n = ++id;
      waiting.set(n, (m) => (m.error ? rej(new Error(`${method}: ${m.error.message}`)) : res(m.result)));
      ws.send(JSON.stringify({ id: n, method, params }));
    });

    await send('Page.enable');
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
    await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'no-preference' }] });
    await send('Page.navigate', { url });
    await sleep(1500);

    const evaluate = async (expression) => {
      const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
      if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
      return r.result.value;
    };

    /** Centre of the first element matching `selector`, or null. */
    const box = (selector) => evaluate(`(() => {
      const n = document.querySelector(${JSON.stringify(selector)});
      if (!n) return null;
      const r = n.getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2, left: r.left, top: r.top, width: r.width, height: r.height };
    })()`);

    // The standalone asks about the input on first start. Answer GO LIVE so
    // every journey starts on the board; `startQuestion` tells whether it came.
    const startQuestion = await evaluate(`(() => {
      const o = document.getElementById('overlay-dialog');
      if (!o || o.hidden) return false;
      const live = [...o.querySelectorAll('button')].find(b => b.textContent.trim() === 'GO LIVE');
      if (live) live.click();
      return !!live;
    })()`);
    await sleep(300);

    const mouse = (type, x, y, extra = {}) =>
      send('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1, ...extra });

    const click = async (selector) => {
      const b = await box(selector);
      if (!b) throw new Error('No element for ' + selector);
      await mouse('mouseMoved', b.x, b.y);
      await mouse('mousePressed', b.x, b.y);
      await mouse('mouseReleased', b.x, b.y);
      await sleep(250);
    };

    const key = async (k) => {
      await send('Input.dispatchKeyEvent', { type: 'keyDown', key: k, code: k, windowsVirtualKeyCode: k === 'Escape' ? 27 : 0 });
      await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code: k });
      await sleep(200);
    };

    /** PNG of the page or of a clip {x, y, width, height}. */
    const screenshot = async (clip) => {
      const params = { format: 'png', captureBeyondViewport: false };
      if (clip) params.clip = { ...clip, scale: 1 };
      return Buffer.from((await send('Page.captureScreenshot', params)).data, 'base64');
    };

    /**
     * Fills the chain the way a player would: pedal capture, amp capture,
     * the cab IR it brings along, and a few effects, all from the demo library.
     */
    const demoBoard = async () => {
      await evaluate(`(async () => {
        const { call } = await import('./bridge.js');
        // Whatever the demo library holds first for a role, so the board
        // never depends on the names of the demo tones.
        const add = async (pos, type, role, gear) => {
          const r = await call('slot.add', { type, role, lane: 0, pos });
          const slot = r?.data?.slot ?? -1;
          if (!gear || slot < 0) return;
          const banks = (await call('library.list', { type, role }))?.data ?? [];
          const bank = banks[0];
          if (bank) await call('slot.load', { slot, bank: bank.title, variant: bank.variants?.[0]?.name, gear });
        };
        await add(1, 'COMP');
        await add(2, 'NAM', 'pedal', 'pedal');
        await add(4, 'NAM', 'amp', 'amp');
        await add(7, 'DELAY');
        await add(9, 'NAM', 'space', 'space');
      })()`);
      // The amp brings its own cab (IR) along and says so; wait until that
      // note has gone by itself.
      for (let i = 0; i < 40; i++) {
        await sleep(200);
        if (await evaluate(`!document.querySelector('#toast-slot .toast')`)) break;
      }
      await sleep(300);
    };

    return { demoBoard, startQuestion, send, evaluate, box, mouse, click, key, screenshot, sleep, errors, close };
  } catch (e) {
    await close();
    throw e;
  }
}
