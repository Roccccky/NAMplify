// Verbindung zum Programm.
//
// Im Programm läuft alles über die Native-Funktionen von JUCE. In einem
// normalen Browser übernimmt die Demo (mock.js).

import { mockCall, mockStart } from './mock.js';

const embedded = typeof window.__JUCE__ !== 'undefined'
              && typeof window.__JUCE__.postMessage === 'function'
              && window.__NAMPLIFY_EMBEDDED__ === true;

export const isEmbedded = embedded;

const listeners = new Map();

export function on (event, fn) {
  if (!listeners.has(event)) listeners.set(event, new Set());
  listeners.get(event).add(fn);
  return () => listeners.get(event).delete(fn);
}

export function emitLocal (event, payload) {
  const set = listeners.get(event);
  if (set) for (const fn of set) { try { fn(payload); } catch (e) { console.error(e); } }
}

// Eingebettet
let nextId = 1;
const pending = new Map();

if (embedded) {
  const backend = window.__JUCE__.backend;

  backend.addEventListener('__juce__complete', ({ promiseId, result }) => {
    const entry = pending.get(promiseId);
    if (!entry) return;
    pending.delete(promiseId);
    clearTimeout(entry.timer);
    entry.resolve(result);
  });

  for (const event of ['meters', 'structure', 'notice', 't3kProgress'])
    backend.addEventListener(event, (payload) => emitLocal(event, payload));
}

export function call (cmd, payload = {}) {
  const request = { cmd, ...payload };

  if (!embedded) return mockCall(request);

  return new Promise((resolve) => {
    const id = nextId++;

    // JUCE liefert Antworten nur, solange die Seite sichtbar ist. Ein Aufruf
    // ohne Antwort darf die UI nicht blockieren.
    const timer = setTimeout(() => {
      if (!pending.has(id)) return;
      pending.delete(id);
      resolve({ ok: false, error: 'timeout' });
    }, 30000);

    pending.set(id, { resolve, timer });

    window.__JUCE__.backend.emitEvent('__juce__invoke', {
      name: 'nmp',
      params: [request],
      resultId: id,
    });
  });
}

/// Liefert data oder wirft.
export async function ask (cmd, payload) {
  const reply = await call(cmd, payload);
  if (!reply || reply.ok !== true) throw new Error(reply?.error || 'unknown');
  return reply.data;
}

/// Wie ask, aber ohne Werfen: liefert [data, error].
export async function tryAsk (cmd, payload) {
  const reply = await call(cmd, payload);
  if (!reply || reply.ok !== true) return [null, reply?.error || 'unknown'];
  return [reply.data, null];
}

if (!embedded) mockStart(emitLocal);
