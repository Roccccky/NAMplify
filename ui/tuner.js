// Stimmgerät. Eine Karte über dem Brett, dahinter dunkelt das Fenster ab, Klick
// daneben schließt. Es zeigt nur an.

import { el } from './controls.js';
import { store, paramOn, setParamUnit, notify } from './store.js';
import { call } from './bridge.js';

const SEGMENTS = 21;          // −50 … +50 Cent
const CENTRE = 10;

let node = null, leds = [], arrows = {}, noteEl = null, readoutEl = null;
let strobeCoarse = null, strobeFine = null, modeToggle = null, muteButton = null;
let strobePhase = 0, lastTime = 0;
let trail = new Array(SEGMENTS).fill(0);

export function openTuner () { setParamUnit('tuner', 1).then(() => notify('tuner')); }
export function closeTuner () { setParamUnit('tuner', 0).then(() => notify('tuner')); }

export function renderTuner () {
  const overlay = document.getElementById('overlay-tuner');
  const open = paramOn('tuner');

  if (!open) { overlay.hidden = true; overlay.innerHTML = ''; node = null; return; }
  if (node) return;

  overlay.innerHTML = '';
  overlay.hidden = false;

  const scrim = el('div', 'overlay-scrim', overlay);
  scrim.addEventListener('click', closeTuner);

  // Die Karte im Pedal-Stil mit Anzeigefeld, Statusstreifen, Beschriftung und
  // Fußschalter.
  node = el('div', 'tuner-card', overlay);

  const display = el('div', 'tuner-display', node);
  const strobeMode = store.settings?.tunerDisplay === 1;

  // Skala und Stroboskop liegen übereinander in einem Feld fester Höhe.
  const scale = el('div', 'tuner-scale', display);

  // LEDS
  const head = el('div', 'tuner-scale-head', scale);
  el('span', '', head).textContent = '\u201350';
  el('span', '', head).textContent = '+50';

  const ledRow = el('div', 'tuner-leds', scale);
  leds = [];
  for (let i = 0; i < SEGMENTS; ++i) {
    const led = el('div', 'tuner-led', ledRow);
    if (i === CENTRE) led.dataset.centre = 'true';
    leds.push(el('div', 'tuner-led-glow', led));
  }
  const ledBox = el('div', '', scale);   // Halter, damit hidden beides trifft
  ledBox.append(head, ledRow);
  ledBox.hidden = strobeMode;

  // STROBE. Die Bahnen bekommen ihr Layout aus dem Stylesheet. Ein display:
  // flex im Code würde display: none von hidden überschreiben.
  const strobeBox = el('div', 'tuner-strobe', scale);
  strobeBox.hidden = !strobeMode;

  strobeCoarse = el('div', 'strobe-band', strobeBox);
  el('div', 'strobe-stripes', strobeCoarse);
  strobeFine = el('div', 'strobe-band', strobeBox);
  strobeFine.dataset.fine = 'true';
  el('div', 'strobe-stripes', strobeFine);

  // Tonanzeige
  const noteRow = el('div', 'tuner-note-row', display);

  const left = el('div', 'tuner-arrow', noteRow);
  left.dataset.side = 'left';
  arrows.left = left;

  noteEl = el('div', 'tuner-note', noteRow);
  noteEl.textContent = '\u2014';

  const right = el('div', 'tuner-arrow', noteRow);
  right.dataset.side = 'right';
  arrows.right = right;

  readoutEl = el('div', 'tuner-readout', display);
  readoutEl.textContent = '\u2014';

  // Kippschalter LEDS ⇄ STROBE zwischen Anzeige und Beschriftung. Die Wahl
  // bleibt über den Neustart erhalten.
  const toggleRow = el('div', 'tuner-style', node);

  modeToggle = el('div', 'toggle', toggleRow);
  modeToggle.dataset.on = strobeMode ? 'true' : 'false';
  modeToggle.style.setProperty('--knob-colour', 'var(--c-gate)');
  const a = el('span', 'toggle-text toggle-text-a', modeToggle);
  a.textContent = 'LEDS';
  const rail = el('div', 'toggle-rail', modeToggle);
  el('div', 'toggle-knob', rail);
  const b = el('span', 'toggle-text toggle-text-b', modeToggle);
  b.textContent = 'STROBE';

  modeToggle.addEventListener('click', async () => {
    const next = modeToggle.dataset.on === 'true' ? 0 : 1;
    modeToggle.dataset.on = next ? 'true' : 'false';
    ledBox.hidden = !!next;
    strobeBox.hidden = !next;
    await call('settings.set', { tunerDisplay: next });
    if (store.settings) store.settings.tunerDisplay = next;
  });

  // Fuß
  const foot = el('div', 'tuner-foot', node);

  const stripe = el('div', 'card-stripe', foot);
  const stripeFill = el('div', 'card-stripe-fill', stripe);
  stripeFill.style.background = 'var(--c-gate)';
  stripeFill.style.opacity = '1';

  const legend = el('div', 'card-legend', foot);
  el('div', 'card-legend-brand', legend).textContent = 'NAMPLIFY';
  const legendName = el('div', 'card-legend-name', legend);
  legendName.textContent = 'TUNER';
  legendName.style.color = 'var(--c-gate)';

  const row = el('div', 'footswitch-row', foot);

  const closeButton = el('button', 'footswitch', row);
  el('div', 'footswitch-led', closeButton);
  closeButton.style.setProperty('--knob-colour', 'var(--c-gate)');
  closeButton.dataset.on = 'true';
  closeButton.title = 'Close the tuner';
  closeButton.addEventListener('click', closeTuner);

  // Daneben ein MUTE-Taster (leuchtend = stumm), für Stimmen und Umstecken.
  muteButton = el('button', 'tuner-mute', row);
  muteButton.textContent = 'MUTE';
  muteButton.addEventListener('click', async () => {
    const next = paramOn('mute') ? 0 : 1;
    await setParamUnit('mute', next);
    // Wer im Stimmgerät den Taster drückt, legt die Wahl fest. Das Programm
    // merkt sie sich beim Schließen.
    await call('settings.set', { tunerMute: !!next });
    notify('mute');
  });

  lastTime = performance.now();
}

// Kleines Stimmgerät unten links als eigene Karte. Der Kern misst immer mit,
// das große Stimmgerät ist nur eine größere Anzeige und schaltet stumm. Oben
// links der Schalter (an/aus, je Rechner gespeichert, aus heißt auch: keine
// Rechnung), rechts LEDS ⇄ STROBE wie beim großen.
const MINI_KEY = 'namplify-mini-tuner';
let mini = null, miniPhase = 0, miniLast = 0;

function miniWanted () {
  try { return localStorage.getItem(MINI_KEY) !== 'off'; } catch { return true; }
}

export function mountMiniTuner (host) {
  host.innerHTML = '';
  const root = el('div', 'mini-tuner', host);
  root.dataset.on = miniWanted() ? 'true' : 'false';

  const head = el('div', 'mini-tuner-head', root);
  const power = el('button', 'power', head);
  power.title = 'Show the tuner here';
  power.addEventListener('click', () => {
    const on = root.dataset.on !== 'true';
    root.dataset.on = on ? 'true' : 'false';
    power.dataset.on = on ? 'true' : 'false';
    try { localStorage.setItem(MINI_KEY, on ? 'on' : 'off'); } catch {}
    call('tuner.watch', { on });
    if (!on) blankMini();
  });
  power.dataset.on = root.dataset.on;
  // Das Programm rechnet die Tonerkennung nur, solange sie angezeigt wird.
  call('tuner.watch', { on: root.dataset.on === 'true' });
  el('span', 'mini-tuner-name', head).textContent = 'TUNER';
  el('span', 'spacer', head);

  const style = el('button', 'toggle mini-tuner-style', head);
  style.title = 'LEDs or strobe, the same choice as the big tuner';
  el('span', 'toggle-text toggle-text-a', style).textContent = 'LEDS';
  el('div', 'toggle-knob', el('div', 'toggle-rail', style));
  el('span', 'toggle-text toggle-text-b', style).textContent = 'STROBE';
  style.addEventListener('click', async () => {
    const next = store.settings?.tunerDisplay === 1 ? 0 : 1;
    if (store.settings) store.settings.tunerDisplay = next;
    paintMiniStyle();
    await call('settings.set', { tunerDisplay: next });
  });

  const row = el('div', 'mini-tuner-row', root);
  // Ton und Cent übereinander, neben beiden Schaltern ist kein Platz.
  const pitch = el('div', 'mini-tuner-pitch', row);
  const note = el('div', 'mini-tuner-note', pitch);
  const cents = el('div', 'mini-tuner-cents', pitch);
  const ledRow = el('div', 'mini-tuner-leds well', row);
  const miniLeds = [];
  for (let i = 0; i < SEGMENTS; ++i) {
    const led = el('i', '', ledRow);
    if (i === CENTRE) led.dataset.centre = 'true';
    miniLeds.push(led);
  }
  const bands = el('div', 'mini-tuner-bands', row);
  const coarse = el('div', 'mini-tuner-band well', bands);
  const coarseStripes = el('div', 'mini-tuner-stripes', coarse);
  el('i', 'mini-tuner-mark', coarse);
  const fine = el('div', 'mini-tuner-band well', bands);
  fine.dataset.fine = 'true';
  const fineStripes = el('div', 'mini-tuner-stripes', fine);
  el('i', 'mini-tuner-mark', fine);

  mini = { root, note, cents, coarseStripes, fineStripes, style, ledRow, bands, leds: miniLeds };
  paintMiniStyle();
  blankMini();
}

/// LEDS oder STROBE wie in den Einstellungen, auch wenn das große Stimmgerät es
/// ändert.
export function paintMiniStyle () {
  if (!mini) return;
  const strobe = store.settings?.tunerDisplay === 1;
  mini.style.dataset.on = strobe ? 'true' : 'false';
  mini.ledRow.hidden = strobe;
  mini.bands.hidden = !strobe;
}

function blankMini () {
  if (!mini) return;
  mini.root.dataset.state = 'none';
  mini.note.textContent = '—';
  mini.cents.textContent = '';
  for (const l of mini.leds) l.dataset.lit = 'false';
}

function updateMini (t) {
  if (!mini || !mini.root.isConnected) return;
  paintMiniStyle();
  if (mini.root.dataset.on !== 'true') return;
  const now = performance.now();
  const dt = Math.min(.1, (now - miniLast) / 1000);
  miniLast = now;
  if (!t?.has) { blankMini(); return; }
  const c = Math.max(-50, Math.min(50, t.cents));
  const inTune = Math.abs(c) < 3;
  mini.root.dataset.state = inTune ? 'good' : 'some';
  mini.note.innerHTML = `${t.note}<sub>${t.octave}</sub>`;
  mini.cents.textContent = `${c >= 0 ? '+' : ''}${c.toFixed(0)} ct`;
  // Wie beim großen: das Muster läuft mit der Schwebung und steht still, wenn
  // der Ton stimmt. Nach rechts zu hoch, nach links zu tief. Das untere Band
  // läuft viermal schneller.
  miniPhase += t.freq * (Math.pow(2, c / 1200) - 1) * dt;
  miniPhase -= Math.floor(miniPhase);
  mini.coarseStripes.style.transform = `translateX(${(miniPhase - 1) * 12}px)`;
  const at = Math.round((c + 50) / 100 * (SEGMENTS - 1));
  mini.leds.forEach((l, i) => { l.dataset.lit = i === at ? 'true' : 'false'; });
  mini.fineStripes.style.transform = `translateX(${(((miniPhase * 4) % 1) - 1) * 6}px)`;
}

export function updateTuner (m) {
  updateMini(m?.tuner);
  if (!node || !m?.tuner) return;

  const t = m.tuner;
  const now = performance.now();
  const dt = Math.min(.1, (now - lastTime) / 1000);
  lastTime = now;

  muteButton.dataset.on = m.muted ? 'true' : 'false';

  if (!t.has) {
    noteEl.textContent = '—';
    noteEl.dataset.state = 'none';
    readoutEl.textContent = ' ';
    arrows.left.dataset.on = 'false';
    arrows.right.dataset.on = 'false';
    arrows.left.dataset.good = 'false';
    arrows.right.dataset.good = 'false';
    strobeCoarse.dataset.lit = 'false';
    strobeFine.dataset.lit = 'false';
    for (const g of leds) g.style.opacity = '0';
    trail.fill(0);
    return;
  }

  const cents = Math.max(-50, Math.min(50, t.cents));
  const inTune = Math.abs(cents) < 3;

  noteEl.innerHTML = `${t.note}<sub>${t.octave}</sub>`;
  readoutEl.textContent = `${cents >= 0 ? '+' : ''}${cents.toFixed(0)} ct · ${t.freq.toFixed(1)} Hz`;

  noteEl.dataset.state = inTune ? 'good' : 'some';

  // Getroffen leuchten beide Zeiger grün, sonst nur der in Drehrichtung, rot.
  arrows.left.dataset.on = (inTune || cents > 3) ? 'true' : 'false';
  arrows.right.dataset.on = (inTune || cents < -3) ? 'true' : 'false';
  arrows.left.dataset.good = inTune ? 'true' : 'false';
  arrows.right.dataset.good = inTune ? 'true' : 'false';

  strobeCoarse.dataset.lit = 'true';
  strobeFine.dataset.lit = 'true';

  // LEDS
  const index = Math.round(CENTRE + cents / 50 * CENTRE);

  // Getroffen: ein grünes Segment in der Mitte, kein Schweif.
  if (inTune) {
    trail.fill(0);
    leds.forEach((g, i) => {
      g.style.opacity = i === CENTRE ? '1' : '0';
      g.style.background = 'var(--ok)';
    });
  } else {
    // Daneben: ein rotes Segment als Zeiger mit kurzem Nachleuchten, das von
    // der Mitte weg ausblendet.
    for (let i = 0; i < SEGMENTS; ++i) trail[i] = Math.max(0, trail[i] - dt * 3.2);
    trail[index] = 1;

    // Alles zwischen Zeiger und Mitte gehört nicht zum Schweif.
    leds.forEach((g, i) => {
      const outward = index > CENTRE ? i > index : i < index;
      const value = (i === index) ? 1 : (outward ? trail[i] : 0);
      g.style.opacity = String(value);
      g.style.background = 'var(--error)';
    });
  }

  // STROBE. Das Muster läuft mit der Schwebung f · (2^(ct/1200) − 1), deshalb
  // hängt die Geschwindigkeit an der Tonhöhe: tief träge, hoch flink.
  const beatHz = t.freq * (Math.pow(2, cents / 1200) - 1);

  strobePhase += beatHz * dt;
  // Der Versatz wird auf eine Streifenbreite zurückgeführt, nicht
  // abgeschnitten, damit das Muster beim Umlaufen nicht springt.
  strobePhase -= Math.floor(strobePhase);

  // Verschoben wird um genau eine Streifenbreite. Sie steht als --strobe-step
  // am Streifenfeld und skaliert mit der Pedalgröße.
  shiftStripes(strobeCoarse, strobePhase);
  // Das untere Band läuft viermal schneller.
  shiftStripes(strobeFine, (strobePhase * 4) % 1);
}

/// Ein Streifenfeld um phase (0…1) einer Streifenbreite verschieben.
///
/// Die Streifenbreite wird am Feld gemessen: das Feld ragt links und rechts je
/// zwei Streifenbreiten über sein Band hinaus (--strobe-step in overlays.css),
/// der Überstand durch vier ist also eine. getComputedStyle auf --strobe-step
/// liefert nur das unausgerechnete calc(…).
function shiftStripes (band, phase) {
  const node = band?.querySelector('.strobe-stripes');
  if (!node) return;
  const step = (node.offsetWidth - band.clientWidth) / 4;
  if (step <= 0) return;
  node.style.transform = `translateX(${(phase * step).toFixed(2)}px)`;
}
