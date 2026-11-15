// Kurve des globalen EQ.

import { el } from './controls.js';
import { curveDb } from './biquad.js';
import { call, tryAsk } from './bridge.js';
import { showDialog } from './dialogs.js';

/// Sieben Punkte, jeder in eigener Farbe, damit sie unterscheidbar sind.
const BAND_COLOURS = ['#e2574c', '#d98324', '#c9a227', '#4caf6d',
                      '#3fb0a5', '#5a8fd8', '#9b6cd6'];

const SHAPES = ['BELL', 'LOW SHELF', 'HIGH SHELF', 'LOW CUT', 'HIGH CUT'];

/// Die Achse reicht über den Regelbereich hinaus, damit man auch am Rand sieht,
/// was die Flanke eines Schnitts tut.
const F_MIN = 12, F_MAX = 28000;
const DB_RANGE = 18;         // Regelbereich ist ±12
const SAMPLE_RATE = 48000;

export function buildEqEditor (parent, settings, onChanged) {
  const wrap = el('div', 'eq-editor', parent);
  const graph = el('div', 'eq-graph', wrap);
  const canvas = el('canvas', '', graph);
  const fields = el('div', 'eq-fields', wrap);

  let bands = settings.eqBands.map((b) => ({ ...b }));
  let active = 0;
  let dragging = null;
  let backendCurve = null;

  const xOf = (f, W) => (Math.log(f / F_MIN) / Math.log(F_MAX / F_MIN)) * W;
  const fOf = (x, W) => F_MIN * Math.pow(F_MAX / F_MIN, x / W);
  const yOf = (db, H) => H / 2 - (db / DB_RANGE) * (H / 2);
  const dbOf = (y, H) => ((H / 2 - y) / (H / 2)) * DB_RANGE;

  // Felder
  const fieldNodes = {};
  for (const [key, label] of [['freq', 'FREQUENCY'], ['gain', 'GAIN'],
                              ['q', 'QUALITY'], ['shape', 'MODE']]) {
    const box = el('div', 'eq-field', fields);
    el('div', 'eq-field-label', box).textContent = label;
    const value = el('div', 'eq-field-value', box);
    if (key === 'shape') value.dataset.mode = 'true';
    fieldNodes[key] = value;

    value.addEventListener('click', async () => {
      const b = bands[active];
      if (key === 'shape') {
        // MODE schaltet durch die Filterarten, statt eine Liste aufzuklappen,
        // die die Kurve verdecken würde.
        b.shape = (b.shape + 1) % SHAPES.length;
        push(b);
        return;
      }
      const answer = await showDialog({
        title: label, input: String(round(b[key], key)),
        confirm: 'SET', cancel: 'CANCEL',
      });
      if (answer === null) return;
      const n = parseFloat(answer.replace(',', '.'));
      if (!isFinite(n)) return;
      b[key] = clampField(key, n);
      push(b);
    });
  }

  function round (v, key) {
    if (key === 'freq') return v >= 1000 ? Math.round(v) : v.toFixed(0);
    if (key === 'gain') return v.toFixed(1);
    return v.toFixed(2);
  }

  function clampField (key, v) {
    if (key === 'freq') return Math.min(20000, Math.max(20, v));
    if (key === 'gain') return Math.min(12, Math.max(-12, v));
    return Math.min(12, Math.max(0.1, v));
  }

  async function push (b) {
    const index = bands.indexOf(b);
    const data = await tryAsk('eq.band', { band: index, freq: b.freq, gain: b.gain,
                                           q: b.q, shape: b.shape });
    backendCurve = null;
    if (data?.[0]) { bands = data[0].eqBands.map((x) => ({ ...x })); onChanged?.(data[0]); }
    draw();
  }

  // Ziehen
  graph.addEventListener('pointerdown', (e) => {
    const r = graph.getBoundingClientRect();
    const x = e.clientX - r.left, y = e.clientY - r.top;

    let best = -1, bestD = 24;
    bands.forEach((b, i) => {
      const px = xOf(b.freq, r.width);
      const py = pointY(b, i, r.height, r.width);
      const d = Math.hypot(px - x, py - y);
      if (d < bestD) { bestD = d; best = i; }
    });

    if (best < 0) return;
    active = best;
    dragging = { index: best, pointerId: e.pointerId };
    graph.setPointerCapture(e.pointerId);
    draw();
  });

  graph.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const r = graph.getBoundingClientRect();
    const b = bands[dragging.index];

    // Waagerecht ziehen = Frequenz, senkrecht = Verstärkung.
    b.freq = clampField('freq', fOf(Math.max(0, Math.min(r.width, e.clientX - r.left)), r.width));
    if (b.shape !== 3 && b.shape !== 4)
      b.gain = clampField('gain', dbOf(e.clientY - r.top, r.height));

    draw();
  });

  const stop = (e) => {
    if (!dragging) return;
    const b = bands[dragging.index];
    dragging = null;
    try { graph.releasePointerCapture(e.pointerId); } catch {}
    push(b);
  };
  graph.addEventListener('pointerup', stop);
  graph.addEventListener('pointercancel', stop);

  // Mausrad über einem Punkt = Güte.
  graph.addEventListener('wheel', (e) => {
    const r = graph.getBoundingClientRect();
    const x = e.clientX - r.left, y = e.clientY - r.top;

    let best = -1, bestD = 26;
    bands.forEach((b, i) => {
      const d = Math.hypot(xOf(b.freq, r.width) - x, pointY(b, i, r.height, r.width) - y);
      if (d < bestD) { bestD = d; best = i; }
    });
    if (best < 0) return;

    e.preventDefault();
    active = best;
    const b = bands[best];
    b.q = clampField('q', b.q * (e.deltaY > 0 ? 0.92 : 1.087));
    push(b);
  }, { passive: false });

  // Doppelklick setzt nur dieses Band zurück.
  graph.addEventListener('dblclick', (e) => {
    const r = graph.getBoundingClientRect();
    const x = e.clientX - r.left, y = e.clientY - r.top;

    let best = -1, bestD = 26;
    bands.forEach((b, i) => {
      const d = Math.hypot(xOf(b.freq, r.width) - x, pointY(b, i, r.height, r.width) - y);
      if (d < bestD) { bestD = d; best = i; }
    });
    if (best < 0) return;

    const b = bands[best];
    b.gain = 0;
    b.shape = 0;
    push(b);
  });

  /// Ein Schnitt hat keine Verstärkung: sein Punkt sitzt auf der Kurve bei
  /// seiner Frequenz.
  function pointY (b, i, H, W) {
    if (b.shape === 3 || b.shape === 4)
      return yOf(curveDb(bands, b.freq, SAMPLE_RATE), H);
    return yOf(b.gain, H);
  }

  // Zeichnen
  async function fetchCurve (freqs) {
    const [data] = await tryAsk('eq.response', { freqs });
    // Die Demo antwortet mit db: null, dann rechnet die Seite selbst.
    backendCurve = data?.db || null;
    draw(true);
  }

  function draw (skipFetch = false) {
    const r = graph.getBoundingClientRect();
    if (!r.width || !r.height) return;

    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(r.width * dpr);
    canvas.height = Math.round(r.height * dpr);
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, r.width, r.height);

    const W = r.width, H = r.height;

    // Frequenzachse mit Beschriftung
    ctx.font = '9px -apple-system, sans-serif';
    for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
      const x = xOf(f, W);
      ctx.strokeStyle = 'rgba(255,255,255,.06)';
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
      ctx.fillStyle = 'rgba(255,255,255,.22)';
      ctx.fillText(f >= 1000 ? `${f / 1000}k` : String(f), x + 3, H - 4);
    }

    // Pegellinien, die Null hervorgehoben
    for (let db = -DB_RANGE; db <= DB_RANGE; db += 6) {
      const y = yOf(db, H);
      ctx.strokeStyle = db === 0 ? 'rgba(255,255,255,.22)' : 'rgba(255,255,255,.06)';
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
      if (db !== 0) {
        ctx.fillStyle = 'rgba(255,255,255,.2)';
        ctx.fillText(`${db > 0 ? '+' : ''}${db}`, 3, y - 2);
      }
    }

    // Die Kurve, wie sie wirklich klingt.
    const steps = Math.max(96, Math.floor(W));
    const freqs = Array.from({ length: steps }, (_, i) => fOf((i / (steps - 1)) * W, W));

    if (!backendCurve && !skipFetch) fetchCurve(freqs);

    const db = backendCurve && backendCurve.length === steps
      ? backendCurve
      : freqs.map((f) => curveDb(bands, f, SAMPLE_RATE));

    ctx.beginPath();
    for (let i = 0; i < steps; ++i) {
      const x = (i / (steps - 1)) * W;
      const y = yOf(db[i], H);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = '#d2d2d2';
    ctx.lineWidth = 1.6;
    ctx.stroke();

    ctx.lineTo(W, yOf(0, H));
    ctx.lineTo(0, yOf(0, H));
    ctx.closePath();
    ctx.fillStyle = 'rgba(210,210,210,.07)';
    ctx.fill();

    // Die Punkte
    bands.forEach((b, i) => {
      const x = xOf(b.freq, W);
      const y = pointY(b, i, H, W);
      ctx.beginPath();
      ctx.arc(x, y, i === active ? 7 : 5, 0, Math.PI * 2);
      ctx.fillStyle = BAND_COLOURS[i];
      ctx.fill();
      if (i === active) {
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 1.4;
        ctx.stroke();
      }
      ctx.fillStyle = 'rgba(0,0,0,.75)';
      ctx.font = '8px -apple-system, sans-serif';
      ctx.fillText(String(i + 1), x - 2, y + 3);
    });

    // Felder
    const b = bands[active];
    fieldNodes.freq.textContent = `${round(b.freq, 'freq')} Hz`;
    fieldNodes.gain.textContent = (b.shape === 3 || b.shape === 4)
      ? '—' : `${b.gain >= 0 ? '+' : ''}${b.gain.toFixed(1)} dB`;
    fieldNodes.q.textContent = b.q.toFixed(2);
    fieldNodes.shape.textContent = SHAPES[b.shape];
  }

  wrap.redraw = (nextSettings) => {
    if (nextSettings) bands = nextSettings.eqBands.map((b) => ({ ...b }));
    backendCurve = null;
    draw();
  };

  requestAnimationFrame(() => draw());
  return wrap;
}
