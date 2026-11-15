// Bedienelemente. Jedes gibt es hier genau einmal, damit es überall gleich
// aussieht.

import { icon } from './icons.js';
import {
  def, param, paramUnit, paramValue, setParamUnit, resetParam, defaultUnit,
  beginGesture, endGesture, valueText, unitToValue, subscribe, notify,
} from './store.js';
import { call } from './bridge.js';

const NS = 'http://www.w3.org/2000/svg';

/// Ein voller Weg entspricht etwa einer Handbreite.
const FULL_TRAVEL_PX = 150;
/// Mit ⌥ deutlich feiner.
const FINE_FACTOR = 0.18;
/// Wie lange der Wert nach dem Loslassen stehen bleibt.
const VALUE_HOLD_MS = 900;

function el (tag, cls, parent) {
  const node = document.createElement(tag);
  if (cls) node.className = cls;
  if (parent) parent.appendChild(node);
  return node;
}

function svgEl (tag, attrs) {
  const node = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
  return node;
}

// Drehregler
export function makeKnob (paramId, opts = {}) {
  const d = def(paramId);
  const root = el('div', 'knob');
  root.dataset.small = opts.small ? 'true' : 'false';
  root.dataset.showvalue = 'false';
  root.dataset.off = 'false';
  if (opts.colour) root.style.setProperty('--knob-colour', opts.colour);

  const dial = el('div', 'knob-dial', root);

  // Ring von −135° bis +135°.
  const R = 42, CX = 50, CY = 50;
  const A0 = -225 * Math.PI / 180, A1 = 45 * Math.PI / 180;

  const svg = svgEl('svg', { viewBox: '0 0 100 100' });

  const defs = svgEl('defs', {});
  const grad = svgEl('radialGradient', { id: 'knobFace_' + paramId, cx: '38%', cy: '30%', r: '75%' });
  // Neutrales Grau wie das übrige Blech.
  grad.appendChild(svgEl('stop', { offset: '0', 'stop-color': '#4a4a4a' }));
  grad.appendChild(svgEl('stop', { offset: '1', 'stop-color': '#242424' }));
  defs.appendChild(grad);
  svg.appendChild(defs);

  const arcPath = (from, to) => {
    const x0 = CX + R * Math.cos(from), y0 = CY + R * Math.sin(from);
    const x1 = CX + R * Math.cos(to),   y1 = CY + R * Math.sin(to);
    const large = Math.abs(to - from) > Math.PI ? 1 : 0;
    return `M ${x0} ${y0} A ${R} ${R} 0 ${large} 1 ${x1} ${y1}`;
  };

  const ringBg = svgEl('path', { class: 'knob-ring-bg', d: arcPath(A0, A1),
                                 fill: 'none', 'stroke-width': 6, 'stroke-linecap': 'round' });
  const ringFg = svgEl('path', { class: 'knob-ring-fg', d: '',
                                 fill: 'none', 'stroke-width': 6, 'stroke-linecap': 'round' });
  const cap = svgEl('circle', { class: 'knob-cap', cx: CX, cy: CY, r: 30,
                                fill: `url(#knobFace_${paramId})` });
  // Der Zeiger ist kräftiger als der Kranz: er zeigt den Wert, der Kranz nur
  // den Weg.
  const pointer = svgEl('line', { class: 'knob-pointer', x1: CX, y1: CY, x2: CX, y2: CY,
                                  'stroke-width': 5 });

  svg.append(ringBg, ringFg, cap, pointer);

  // Regler mit festen Stellungen: an jeder Stellung ein kurzer Strich außen am
  // Ring, der Zeiger rastet darauf ein.
  if (d?.kind === 'choice' && d.max > 0 && d.max <= 12) {
    for (let i = 0; i <= d.max; ++i) {
      const a = A0 + (A1 - A0) * (i / d.max);
      const c = Math.cos(a), sn = Math.sin(a);
      svg.appendChild(svgEl('line', { class: 'knob-detent',
        x1: CX + 46 * c, y1: CY + 46 * sn, x2: CX + 50 * c, y2: CY + 50 * sn,
        'stroke-width': 2, 'stroke-linecap': 'round' }));
    }
  }

  dial.appendChild(svg);

  const caption = el('div', 'knob-caption', root);
  const nameEl = el('span', 'knob-name label', caption);
  const valueEl = el('span', 'knob-value label', caption);
  nameEl.textContent = opts.label !== undefined
    ? opts.label
    : (d ? d.name.replace(/^.*\s/, '') : paramId);

  // Anzeige
  let holdTimer = null;

  function showValue (on) {
    clearTimeout(holdTimer);
    if (on) { root.dataset.showvalue = 'true'; return; }
    holdTimer = setTimeout(() => { root.dataset.showvalue = 'false'; }, VALUE_HOLD_MS);
  }

  function render () {
    const dd = def(paramId);
    // Ein Regler mit festen Stellungen zeigt auf die Stellung, nicht auf den
    // Rohwert dazwischen.
    const raw = paramUnit(paramId);
    const u = (dd?.kind === 'choice' && dd.max > 0)
      ? Math.round(raw * dd.max) / dd.max
      : raw;
    const v = paramValue(paramId);
    const angle = A0 + (A1 - A0) * u;

    // Am linken Anschlag ist ein Regler mit gekoppeltem An/Aus aus und zeigt
    // OFF statt einer Zahl.
    const off = opts.offAtMin && u <= 1e-4;
    root.dataset.off = off ? 'true' : 'false';

    ringFg.setAttribute('d', u > 0.001 ? arcPath(A0, angle) : '');
    // Flacher Knopf: der Zeiger ist ein kurzer Strich am Rand, die Mitte bleibt
    // leer.
    const c = Math.cos(angle), s = Math.sin(angle);
    pointer.setAttribute('x1', CX + 26 * c);
    pointer.setAttribute('y1', CY + 26 * s);
    pointer.setAttribute('x2', CX + 40 * c);
    pointer.setAttribute('y2', CY + 40 * s);

    valueEl.textContent = off ? 'OFF' : (opts.format ? opts.format(v) : valueText(dd, v));
  }

  // Ziehen
  let dragging = false, startY = 0, startUnit = 0, moved = false;

  dial.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    // Der Zug bleibt am Regler, auch wenn der Zeiger die Karte verlässt.
    dial.setPointerCapture(e.pointerId);
    dragging = true;
    moved = false;
    startY = e.clientY;
    startUnit = paramUnit(paramId);
    showValue(true);
    // Ein Zug ist eine Geste für den Host (Undo, Automation).
    beginGesture(paramId);
  });

  dial.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const dy = startY - e.clientY;
    if (Math.abs(dy) > 2) moved = true;
    const factor = (e.altKey ? FINE_FACTOR : 1) / FULL_TRAVEL_PX;
    const next = Math.min(1, Math.max(0, startUnit + dy * factor));
    setParamUnit(paramId, next).then(render);
  });

  const stop = (e) => {
    if (!dragging) return;
    dragging = false;
    try { dial.releasePointerCapture(e.pointerId); } catch {}
    endGesture(paramId);
    showValue(false);
  };
  dial.addEventListener('pointerup', stop);
  dial.addEventListener('pointercancel', stop);

  // Der Startwert wird bei jedem Zug neu genommen, damit der Regler nach dem
  // Anschlag nicht klebt.

  dial.addEventListener('pointerenter', () => { if (!dragging) showValue(true); });
  dial.addEventListener('pointerleave', () => { if (!dragging) showValue(false); });

  // Doppelklick setzt auf den Werkswert zurück.
  dial.addEventListener('dblclick', (e) => {
    e.preventDefault();
    resetParam(paramId).then(render);
    showValue(true);
    showValue(false);
  });

  // Rechtsklick lernt einen MIDI-CC an, direkt am Regler.
  dial.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    e.stopPropagation();
    root.dataset.learning = 'true';
    call('param.learn', { id: paramId });
    setTimeout(() => { root.dataset.learning = 'false'; }, 6000);
  });

  root.dataset.paramId = paramId;
  root.render = render;
  root.setDisabled = (v, why) => {
    root.dataset.disabled = v ? 'true' : 'false';
    if (why) root.title = why; else root.removeAttribute('title');
  };
  render();
  return root;
}

// Fader
export function makeFaderBank (paramIds, labels, opts = {}) {
  const root = el('div', 'fader-bank');
  const faders = paramIds.map((id, i) => makeFader(id, labels[i], opts));
  for (const f of faders) root.appendChild(f);
  root.render = () => faders.forEach((f) => f.render());
  return root;
}

function makeFader (paramId, label, opts = {}) {
  const d = def(paramId);
  const root = el('div', 'fader');
  if (opts.colour) root.style.setProperty('--knob-colour', opts.colour);

  const track = el('div', 'fader-track', root);
  el('div', 'fader-mid', track);
  const fill = el('div', 'fader-fill', track);
  const cap = el('div', 'fader-cap', track);
  const labelEl = el('div', 'fader-label', root);
  labelEl.textContent = label;
  const valueEl = el('div', 'fader-value', root);
  root.dataset.showvalue = 'false';

  function render () {
    const u = paramUnit(paramId);
    // Die Kappe hängt an der Unterkante und wird um ihre halbe Höhe
    // hochgesetzt, damit ihre Mitte auf dem Wert sitzt.
    cap.style.bottom = `${u * 100}%`;

    // Der Balken wächst aus der Mitte nach oben oder unten.
    const half = u - .5;
    const pct = Math.abs(half) * 100;
    fill.style.height = `${pct}%`;
    fill.style.top = `${half > 0 ? 50 - pct : 50}%`;

    valueEl.textContent = valueText(def(paramId), paramValue(paramId));
  }

  let dragging = false, startY = 0, startUnit = 0, holdTimer = null;

  // Überfahren oder Anfassen zeigt den Wert statt der Beschriftung, mit
  // derselben Haltezeit wie am Regler.
  function showValue (on) {
    clearTimeout(holdTimer);
    if (on) { root.dataset.showvalue = 'true'; return; }
    holdTimer = setTimeout(() => { root.dataset.showvalue = 'false'; }, VALUE_HOLD_MS);
  }
  track.addEventListener('pointerenter', () => { if (!dragging) showValue(true); });
  track.addEventListener('pointerleave', () => { if (!dragging) showValue(false); });

  track.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    track.setPointerCapture(e.pointerId);
    dragging = true;
    startY = e.clientY;
    startUnit = paramUnit(paramId);
    beginGesture(paramId);
    showValue(true);
  });
  track.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const h = track.getBoundingClientRect().height || 100;
    const dy = (startY - e.clientY) / h * (e.altKey ? FINE_FACTOR : 1);
    setParamUnit(paramId, Math.min(1, Math.max(0, startUnit + dy))).then(render);
  });
  const stop = (e) => {
    if (!dragging) return;
    dragging = false;
    try { track.releasePointerCapture(e.pointerId); } catch {}
    endGesture(paramId);
    showValue(false);
  };
  track.addEventListener('pointerup', stop);
  track.addEventListener('pointercancel', stop);
  track.addEventListener('dblclick', () => resetParam(paramId).then(render));
  track.addEventListener('contextmenu', (e) => {
    e.preventDefault(); e.stopPropagation();
    call('param.learn', { id: paramId });
  });

  root.render = render;
  render();
  return root;
}

// Kippschalter
export function makeToggle (paramId, textA, textB, opts = {}) {
  const root = el('div', 'toggle');
  if (opts.colour) root.style.setProperty('--knob-colour', opts.colour);

  const a = el('span', 'toggle-text toggle-text-a', root);
  const rail = el('div', 'toggle-rail', root);
  el('div', 'toggle-knob', rail);
  const b = el('span', 'toggle-text toggle-text-b', root);
  a.textContent = textA;
  b.textContent = textB;

  function render () { root.dataset.on = paramValue(paramId) >= .5 ? 'true' : 'false'; }

  // Die ganze Zeile schaltet, die Schiene allein ist zu klein.
  root.addEventListener('click', () => {
    setParamUnit(paramId, paramValue(paramId) >= .5 ? 0 : 1).then(() => { render(); notify('toggle'); });
  });

  root.render = render;
  render();
  return root;
}

/// Kippschalter, dessen Beschriftung den Zustand zeigt (z. B. NORM/RAW): immer
/// nur ein Wort. Als schmale Reglerspalte mit kurzer Schiene in Ringbreite,
/// damit er neben GAIN und VOL in die Reihe passt.
export function makeStateToggle (paramId, textOn, textOff, opts = {}) {
  const root = el('button', 'norm-knob');
  if (opts.colour) root.style.setProperty('--knob-colour', opts.colour);

  // Der Schalter sitzt in einem Feld von Ringhöhe, damit seine Beschriftung auf
  // einer Linie mit GAIN und VOL steht.
  const dial = el('div', 'norm-dial', root);
  const rail = el('div', 'norm-rail', dial);
  el('div', 'norm-dot', rail);
  // Dieselbe Beschriftungsklasse wie unter einem Regler.
  const cap = el('div', 'knob-caption', root);
  const capText = el('span', 'knob-name label', cap);

  function render () {
    const on = paramValue(paramId) >= .5;
    root.dataset.on = on ? 'true' : 'false';
    capText.textContent = on ? textOn : textOff;
  }

  root.addEventListener('click', () => setParamUnit(paramId, paramValue(paramId) >= .5 ? 0 : 1).then(render));
  root.render = render;
  render();
  return root;
}

// Stufenschalter
export function makeSelector (paramId, opts = {}) {
  const d = def(paramId);
  const terms = opts.terms || d?.choices || [];
  const root = el('div', 'selector');
  if (opts.colour) root.style.setProperty('--knob-colour', opts.colour);

  const termRow = el('div', 'selector-terms', root);
  const rail = el('div', 'selector-rail', root);
  const dot = el('div', 'selector-dot', rail);

  const termEls = terms.map((t, i) => {
    const node = el('div', 'selector-term', termRow);
    node.textContent = t;
    // Begriff und Raste sind beide anklickbar.
    node.addEventListener('click', () => select(i));
    return node;
  });

  terms.forEach((_, i) => {
    const detent = el('div', 'selector-detent', rail);
    detent.style.left = `${positionOf(i)}%`;
  });

  /// Jede Stufe bekommt eine gleich breite Spalte, Raste und Knopf stehen in
  /// deren Mitte, genau unter ihrem Begriff.
  function positionOf (i) {
    return terms.length <= 1 ? 50 : ((i + .5) / terms.length) * 100;
  }

  function select (i) {
    setParamUnit(paramId, terms.length <= 1 ? 0 : i / (terms.length - 1))
      .then(() => { render(); if (opts.onChange) opts.onChange(i); notify('selector'); });
  }

  rail.addEventListener('click', (e) => {
    const r = rail.getBoundingClientRect();
    const x = ((e.clientX - r.left) / r.width) * 100;
    let best = 0, bestD = 1e9;
    terms.forEach((_, i) => {
      const dd = Math.abs(positionOf(i) - x);
      if (dd < bestD) { bestD = dd; best = i; }
    });
    select(best);
  });

  function render () {
    const i = Math.round(paramValue(paramId));
    dot.style.left = `${positionOf(i)}%`;
    termEls.forEach((node, k) => { node.dataset.on = k === i ? 'true' : 'false'; });
  }

  root.render = render;
  render();
  return root;
}

// Display
/// Pfeilsymbol aus Pixelquadraten, passend zur Monospace-Schrift daneben.
const PFEIL_ZEILEN = [[2], [1, 2], [0, 1, 2], [1, 2], [2]];

function pfeil (rechts, onClick) {
  const NS = 'http://www.w3.org/2000/svg';
  const s = document.createElementNS(NS, 'svg');
  s.setAttribute('class', 'display-arrow' + (rechts ? ' right' : ''));
  s.setAttribute('viewBox', '0 0 3 5');
  for (let y = 0; y < 5; y++)
    for (const x of PFEIL_ZEILEN[y]) {
      const r = document.createElementNS(NS, 'rect');
      r.setAttribute('x', x);
      r.setAttribute('y', y);
      r.setAttribute('width', 1);
      r.setAttribute('height', 1);
      s.appendChild(r);
    }
  s.addEventListener('click', (e) => { e.stopPropagation(); onClick(); });
  return s;
}

/// Anzeigefeld der Karte: zwei Zeilen, jede mit eigenen Pfeilen links und
/// rechts, der Name mittig dazwischen.
export function makeDisplay ({ colour, onPick, onStep }) {
  const root = el('div', 'display');
  if (colour) root.style.setProperty('--display-colour', colour);

  const zeile = (name) => {
    const row = el('div', 'display-row display-' + name, root);
    row.appendChild(pfeil(false, () => onStep(name, -1)));
    const t = el('div', 'display-text', row);
    // Klick auf den Text öffnet die Auswahlliste.
    t.addEventListener('click', (e) => { e.stopPropagation(); onPick(name, root); });
    row.appendChild(pfeil(true, () => onStep(name, 1)));
    return t;
  };

  const bank = zeile('bank');
  const preset = zeile('preset');

  root.setLines = (bankText, presetText) => {
    bank.textContent = bankText;
    preset.textContent = presetText;
  };
  // Die Liste legt sich auf das Feld, aus dem sie kommt.
  root.well = root;
  return root;
}

// Fußschalter
export function makeFootswitch ({ colour, small, onTap, onHold, title }) {
  const root = el('button', 'footswitch' + (small ? ' footswitch-small' : ''));
  if (colour) root.style.setProperty('--knob-colour', colour);
  if (title) root.title = title;
  el('div', 'footswitch-led', root);

  let holdTimer = null, held = false;

  root.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    e.stopPropagation();
    held = false;
    if (onHold) holdTimer = setTimeout(() => { held = true; onHold(); }, 550);
  });

  const finish = (e) => {
    e?.stopPropagation();
    clearTimeout(holdTimer);
    if (!held && onTap) onTap();
    held = false;
  };

  root.addEventListener('pointerup', finish);
  root.addEventListener('pointerleave', () => { clearTimeout(holdTimer); held = false; });
  root.addEventListener('pointercancel', () => { clearTimeout(holdTimer); held = false; });

  root.setOn = (on) => { root.dataset.on = on ? 'true' : 'false'; };
  return root;
}

// Taktleuchten
export function makeBeatLights (colour) {
  const root = el('div', 'beat-lights');
  const lights = [];
  // Die Eins weiß, die Viertel kräftig. Die aktive Leuchte springt kurz auf,
  // Viertel stärker als Zwischenschläge.
  const pops = [];

  for (let i = 0; i < 16; ++i) {
    const light = el('div', 'beat-light', root);
    if (i === 0) light.dataset.one = 'true';
    else if (i % 4 === 0) light.dataset.quarter = 'true';
    const glow = el('div', 'beat-light-glow', light);
    if (colour && i % 4 !== 0) glow.style.background = colour;
    lights.push(glow);
    pops.push(i % 4 === 0 ? 1.34 : 1.14);
  }

  // Es brennt jeweils eine Leuchte. Der Vorgänger bleibt nur als kurzer
  // Nachschein stehen.
  root.setFlashes = (flashes) => {
    let lit = -1, litV = 0, ghost = -1, ghostV = 0;
    for (let i = 0; i < 16; ++i) {
      const v = flashes?.[i] ?? 0;
      if (v > litV)        { ghost = lit; ghostV = litV; lit = i; litV = v; }
      else if (v > ghostV) { ghost = i;   ghostV = v; }
    }

    for (let i = 0; i < 16; ++i) {
      const g = lights[i];
      if (i === lit && litV > 0) {
        g.style.opacity = '1';
        g.style.transform = `scale(${pops[i]})`;
        g.dataset.ghost = 'false';
      } else if (i === ghost && ghostV > 0 && litV > 0) {
        g.style.opacity = (.3 * (ghostV / litV)).toFixed(3);
        g.style.transform = 'scale(1)';
        g.dataset.ghost = 'true';
      } else {
        g.style.opacity = '0';
        g.style.transform = 'scale(1)';
        g.dataset.ghost = 'false';
      }
    }
  };
  return root;
}

// Pegelpaar
//
// Der Kern liefert den linearen Spitzenwert (1,0 = 0 dBFS). Linear dargestellt
// stünde ein normaler Gitarrenpegel von −20 dBFS bei einem Zehntel. Deshalb in
// dB: −36 dB am Anfang, 0 dB bei drei Vierteln, +12 dB am Ende.
export function pegelSkala (peak) {
  const db = 20 * Math.log10(Math.max(1e-6, peak));
  return Math.min(1, Math.max(0, (db + 36) / 48));
}

export function makeLevelPair (paramId, label) {
  // Regler und Balken werden getrennt geliefert. Der Regler steht mit
  // Beschriftung in der Reglerreihe, der Balken in einem Stapel daneben. Wo
  // beide landen, entscheidet die Kopfleiste.
  const root = el('div', 'level-pair');
  const knob = makeKnob(paramId, { small: true, label: '' });

  // Klick auf die IN-Beschriftung schaltet stumm und wieder live.
  const labelEl = el('div', 'level-caption');
  labelEl.textContent = label;
  root.append(knob, labelEl);

  const meter = el('div', 'level-meter well');
  const fill = el('div', 'level-meter-fill', meter);

  root.bar = meter;

  root.setLevel = (v) => {
    // Liegender Balken in dB, siehe pegelSkala.
    fill.style.setProperty('--v', pegelSkala(v));
  };
  root.setLabel = (text) => { labelEl.textContent = text; };
  root.setMuted = (m) => {
    root.dataset.muted = m ? 'true' : 'false';
    meter.dataset.muted = m ? 'true' : 'false';
  };
  root.knob = knob;
  root.labelEl = labelEl;
  root.render = () => knob.render();
  return root;
}

export { el, svgEl };
