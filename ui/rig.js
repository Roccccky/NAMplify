// Das Brett: Presets links, die Kette oben, die Karte des gewählten Slots
// darunter, die MIDI-Fußschalter unten.
//
// Slots, Typen, Grenzen und Farben kommen vom Programm (store.slots,
// store.types, store.roles). Hier stehen nur Darstellung und Klicks. Jeder
// Klick, der den Aufbau ändert, geht als Befehl ans Programm und kommt als
// neuer Stand zurück.

import { el, makeKnob, pegelSkala } from './controls.js';
import {
  store, def, paramOn, paramValue, setParamUnit, toggleParam, notify,
  slotType, slotColour, slotEnabled, setSlotEnabled, tool,
} from './store.js';
import { call, tryAsk } from './bridge.js';
import { icon } from './icons.js';
import { slotArt } from './block-art.js';
import { showMenu, showDialog, showToast } from './dialogs.js';
import { buildSlotControls } from './cards.js';
import { mountMiniTuner } from './tuner.js';
import { openPicker, stepLibrary, invalidateLibrary } from './wheel.js';

let refreshAll = async () => {};

/// Gewählter Slot. Die Werkzeuge haben ihren eigenen Raum (tools.js).
let selected = 0;
let card = null;           // { key, root, parts, meters, update, apply }
let dragSlot = -1;

export function selectedSlot () { return selected; }

export function selectSlot (n) {
  selected = n;
  cardMode = 'slot';
  renderRig();
}

/// Was die Mitte zeigt: die Karte des gewählten Slots, die MIDI-Karte eines
/// Fußschalters oder alle MIDI-Zuweisungen. Ein Klick auf einen Schalter unten
/// richtet ihn in der Mitte ein.
let cardMode = 'slot';      // 'slot', 'switch', 'midi', 'in', 'out'
let switchSlot = -1;

export function showSwitch (n) { cardMode = 'switch'; switchSlot = n; renderCard(); paintFeet(); paintIo(); }
export function showMidi () { cardMode = 'midi'; renderCard(); paintFeet(); paintIo(); }
/// IN und OUT der Kette sind anklickbar: die Mitte zeigt dann den Eingang
/// (Pegel, stumm) oder den Ausgang (Pegel, Breite).
export function showIo (which) { cardMode = which; renderCard(); paintFeet(); paintIo(); }

export function initRig ({ refresh }) {
  refreshAll = refresh;
}

/// Befehl, der den Aufbau ändert: abschicken, Fehler zeigen, neu lesen.
async function structural (cmd, payload) {
  const [data, error] = await tryAsk(cmd, payload);
  if (error) showToast(error, 'warn');
  invalidateLibrary();
  await refreshAll();
  return data;
}

// Signalweg der Kette
//
// Bei SPLIT ist Spur A dreigeteilt: der Kopf vor dem Abzweig, der parallele
// Teil neben Spur B, der Tail nach dem Zusammenführen. Das Programm kennt dafür
// nur split_after und merge_after, je eine Position in Spur A, und rechnet sie
// wie hier (Rack::process).
//
// Die Marken hängen an Positionen, nicht an Slots. Wer den Aufbau ändert, passt
// die Marken mit an: jeder Abschnitt behält seine Slots, nur der betroffene
// wächst oder schrumpft.

const PARALLEL_MODE = 0, SPLIT_MODE = 1, STEREO_MODE = 2;

function routeShape () {
  const len = store.lanes[0].length;
  const splitMode = paramOn('dual_on') && Math.round(paramValue('lane_b_source')) === SPLIT_MODE;
  if (!splitMode) return { splitMode, len, split: 0, merge: len };
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const split = clamp(Math.round(paramValue('split_after')), 0, len);
  const merge = clamp(Math.round(paramValue('merge_after')), split, len);
  return { splitMode, len, split, merge };
}

/// In welchem Abschnitt eine Position von Spur A liegt.
function sectionAt (pos, r = routeShape()) {
  return pos < r.split ? 'head' : pos < r.merge ? 'par' : 'tail';
}

/// Die Marken als Positionen setzen. Ein Ende ohne Tail heißt OUT.
async function setRoute (split, merge, len) {
  const sd = def('split_after'), md = def('merge_after');
  if (!sd || !md) return;
  const m = merge >= len ? md.max : merge;
  await setParamUnit('split_after', Math.min(split, sd.max) / sd.max);
  await setParamUnit('merge_after', Math.min(m, md.max) / md.max);
}

/// Ein Umbau, der die Abschnitte von Spur A erhält. from/to sagen, aus welchem
/// Abschnitt ein Slot geht und in welchen einer kommt (null = Spur B).
async function structuralKeep (cmd, payload, from, to) {
  const r = routeShape();
  const count = { head: r.split, par: r.merge - r.split, tail: r.len - r.merge };
  const data = await structural(cmd, payload);
  if (!r.splitMode) return data;
  const after = store.lanes[0].length;
  if (after === r.len + (to ? 1 : 0) - (from ? 1 : 0)) {
    if (from) count[from]--;
    if (to) count[to]++;
    await setRoute(count.head, count.head + count.par, after);
    await refreshAll();
  }
  return data;
}

// Zeichnen

export function renderRig () {
  // Ist der gewählte Slot verschwunden, springt die Auswahl auf den ersten.
  if (!store.slots[selected] || store.slots[selected].type === 'EMPTY')
    selected = [...store.lanes[0], ...store.lanes[1]].find((n) => n >= 0) ?? -1;

  renderPresets();
  // Das kleine Stimmgerät wird nur einmal gebaut, sonst hätte es nach jedem
  // Umbau kurz keinen Ton.
  const tunerCard = document.getElementById('mini-tuner-card');
  if (tunerCard && !tunerCard.firstChild) mountMiniTuner(tunerCard);
  renderChain();
  renderCard();
  renderFeet();
}

/// Nur die Werte, nicht der Aufbau, nach einer Reglerbewegung.
export function updateRig () {
  for (const t of document.querySelectorAll('#chain-panel .tile[data-slot]')) {
    const n = Number(t.dataset.slot);
    t.dataset.on = slotEnabled(n) ? 'true' : 'false';
  }
  for (const f of feet) f.paint();
  for (const k of mixerParts) k.render?.();
  paintChainToolbar();
  paintIo();
  if (!card) return;
  for (const p of card.parts || []) p.render?.();
  card.apply?.();
  card.paintHead?.();
  card.paint?.();
}

export function rigMeters (m) {
  // Die stehenden Balken an IN und OUT der Kette.
  for (const bar of document.querySelectorAll('#chain-panel .io-meter')) {
    const v = bar.dataset.io === 'in' ? m?.in : m?.out;
    bar.firstChild?.style.setProperty('--v', pegelSkala(v ?? 0));
  }
  card?.meters?.(m);
  if (m?.drums && card?.beats) card.beats.setFlashes?.(m.drums.flashes);
}

// Presets. Ein Preset umfasst das ganze Brett. Die Liste steht links und ist
// immer sichtbar.

function renderPresets () {
  const panel = document.getElementById('presets-panel');
  panel.innerHTML = '';

  const head = el('div', 'presets-head', panel);
  el('div', 'section-label', head).textContent = 'PRESETS';
  const spacer = el('div', '', head);
  spacer.style.flex = '1';

  // NEW und SAVE stehen oben neben der Anzeige (app.js).

  // Suchen und sortieren. Die Sortierung gehört zum Fenster, nicht ins Preset.
  const tools = el('div', 'presets-tools', panel);
  const search = el('label', 'side-search', tools);
  search.appendChild(icon('search'));
  const input = el('input', '', search);
  input.type = 'search';
  input.placeholder = 'Search';
  input.value = presetQuery;
  input.setAttribute('aria-label', 'Search presets');

  const seg = el('div', 'seg seg-fill', tools);
  for (const [value, text] of PRESET_SORTS) {
    const b = el('button', '', seg);
    b.textContent = text;
    b.dataset.on = presetSort === value ? 'true' : 'false';
    b.addEventListener('click', () => {
      presetSort = value;
      try { localStorage.setItem('namplify-preset-sort', value); } catch { /* nur Bequemlichkeit */ }
      renderPresets();
    });
  }

  const list = el('div', 'presets-list', panel);
  const paintList = () => {
    list.innerHTML = '';
    const all = store.presets?.list || [];
    if (!all.length) {
      el('div', 'presets-empty', list).textContent = 'No presets yet. Save the board with SAVE at the top.';
      return;
    }
    const q = presetQuery.trim().toLowerCase();
    let names = all.filter((name) => name.toLowerCase().includes(q));
    if (presetSort === 'name')
      names = [...names].sort((x, y) => x.localeCompare(y, undefined, { numeric: true, sensitivity: 'base' }));
    if (!names.length) {
      el('div', 'presets-empty', list).textContent = 'Nothing matches.';
      return;
    }
    for (const name of names) presetRow(list, name);
  };
  input.addEventListener('input', () => { presetQuery = input.value; paintList(); });
  paintList();

  // Unten links das kleine Stimmgerät (tuner.js).
}

const PRESET_SORTS = [['list', 'SAVED'], ['name', 'A–Z']];
let presetQuery = '';
let presetSort = (() => {
  try { return localStorage.getItem('namplify-preset-sort') === 'name' ? 'name' : 'list'; }
  catch { return 'list'; }
})();

function presetRow (list, name) {
  const row = el('button', 'preset-row', list);
  row.dataset.on = name === store.presets.current ? 'true' : 'false';
  el('span', 'preset-row-dot', row);
  el('span', 'preset-row-name', row).textContent = name;
  // Mülleimer unter dem Zeiger, löscht sofort.
  const trash = el('span', 'preset-row-trash', row);
  trash.title = 'Delete';
  trash.appendChild(icon('trash'));
  trash.addEventListener('click', async (e) => {
    e.stopPropagation();
    await call('preset.delete', { name });
    await refreshAll();
  });
  row.addEventListener('click', async () => {
    await call('preset.load', { name });
    invalidateLibrary();
    await refreshAll();
  });
}

// Kette

let toolbar = null;
let mixerParts = [];

const laneLetter = (lane) => (lane === 0 ? 'A' : 'B');

function slotLabel (n) {
  const s = store.slots[n];
  // Ein NAM-Slot ohne Capture heißt NAM, die Rolle ergibt sich erst aus der
  // Datei.
  if (s?.type === 'NAM' && (!s.file || s.file.empty)) return 'NAM';
  return s?.label || s?.type || '';
}

function renderChain () {
  const panel = document.getElementById('chain-panel');
  panel.innerHTML = '';
  mixerParts = [];

  toolbar = el('div', 'chain-toolbar', panel);

  // Signalweg als Bildchen: je Slot ein Kästchen, eine Spur mittig, zwei
  // parallele übereinander. SINGLE heißt ohne DUAL.
  const dualOn = paramOn('dual_on');
  const routing = el('div', 'routing', toolbar);
  routing.setAttribute('role', 'radiogroup');
  routing.setAttribute('aria-label', 'Routing');
  toolbar.routes = [];
  for (const r of ROUTES) {
    const b = el('button', '', routing);
    b.setAttribute('role', 'radio');
    b.title = r.title;
    b.setAttribute('aria-label', r.name);
    b.appendChild(pictogram(r.cells));
    b.addEventListener('click', () => setRouting(r));
    toolbar.routes.push([r, b]);
  }
  toolbar.name = el('span', 'routing-name', toolbar);

  // STEREO als eigener Schalter: links Spur A, rechts Spur B. Nur ganz parallel
  // möglich, ein Abzweig nimmt ja aus Spur A.
  const stereo = el('button', 'pill-switch', toolbar);
  el('span', 'pill-switch-dot', stereo);
  el('span', '', stereo).textContent = 'STEREO';
  stereo.title = 'Left input into lane A, right input into lane B';
  stereo.disabled = !(dualOn && currentRoute().key !== 'split');
  stereo.addEventListener('click', async () => {
    const on = Math.round(paramValue('lane_b_source')) === STEREO_MODE;
    await setSource(on ? PARALLEL_MODE : STEREO_MODE);
    await refreshAll();
  });
  toolbar.stereo = stereo;

  el('div', 'spacer', toolbar);

  const body = el('div', 'chain-body', panel);
  if (dualOn) {
    renderRoute(body);
    renderMixer(body);
  } else {
    renderLane(el('div', 'lanes', body), 0);
  }

  paintChainToolbar();
}

function paintChainToolbar () {
  if (!toolbar) return;
  const now = currentRoute();
  for (const [r, b] of toolbar.routes) {
    b.dataset.on = r === now ? 'true' : 'false';
    b.setAttribute('aria-checked', r === now ? 'true' : 'false');
  }
  toolbar.name.textContent = now.name;
  toolbar.stereo.dataset.on = paramOn('dual_on') && Math.round(paramValue('lane_b_source')) === STEREO_MODE
    ? 'true' : 'false';
}

// Kästchen je Bildchen: x = Spalte, y = Zeile (0 oben, 1 unten, .5 mittig).
const serial = (x) => [{ x, y: .5 }];
const branch = (x) => [{ x, y: 0 }, { x, y: 1 }];
const ROUTES = [
  { key: 'single',   name: 'SINGLE',   title: 'One lane',
    cells: [0, 1, 2, 3].flatMap(serial) },
  { key: 'parallel', name: 'PARALLEL', title: 'Two lanes side by side, from the input to the output',
    cells: [0, 1, 2, 3].flatMap(branch) },
  { key: 'split',    name: 'SPLIT · TAIL', title: 'A common head, two lanes, a common tail. Drag SPLIT and MERGE along lane A',
    cells: [...serial(0), ...branch(1), ...branch(2), ...serial(3)] },
];

function currentRoute () {
  if (!paramOn('dual_on')) return ROUTES[0];
  return Math.round(paramValue('lane_b_source')) === SPLIT_MODE ? ROUTES[2] : ROUTES[1];
}

async function setSource (i) {
  const d = def('lane_b_source');
  await setParamUnit('lane_b_source', d?.max ? i / d.max : 0);
}

async function setRouting (r) {
  if (r.key === 'single') {
    if (paramOn('dual_on')) await toggleParam('dual_on');
  } else {
    if (!paramOn('dual_on')) await toggleParam('dual_on');
    const src = Math.round(paramValue('lane_b_source'));
    if (r.key === 'split') await setSource(SPLIT_MODE);
    else if (src === SPLIT_MODE) await setSource(PARALLEL_MODE);
  }
  await refreshAll();
}

function pictogram (cells) {
  const NS = 'http://www.w3.org/2000/svg';
  const width = Math.max(...cells.map((c) => c.x)) + 1;
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${width * 10 - 2} 18`);
  svg.setAttribute('width', String(width * 10 - 2));
  svg.setAttribute('height', '18');
  svg.setAttribute('aria-hidden', 'true');
  for (const c of cells) {
    const r = document.createElementNS(NS, 'rect');
    for (const [k, v] of Object.entries({ x: c.x * 10, y: c.y * 10, width: 8, height: 8, rx: 1.5 }))
      r.setAttribute(k, String(v));
    svg.appendChild(r);
  }
  return svg;
}

/// IN und OUT: ein Knopf, der die Karte des Ein- oder Ausgangs zeigt, mit dem
/// Pegel als stehendem Balken daneben.
const ioNode = (parent, text, title, cls = '') => {
  if (text !== 'IN' && text !== 'OUT') {
    // Spurmarke A / B, nur ein Schild.
    const tag = el('div', `lane-io ${cls}`, parent);
    tag.textContent = text;
    tag.title = title;
    return tag;
  }
  const which = text === 'IN' ? 'in' : 'out';
  const node = el('button', `lane-io ${cls}`, parent);
  node.dataset.io = which;
  node.title = `${title}: click for its level`;
  el('span', 'lane-io-name', node).textContent = text;
  const bar = el('span', 'io-meter well', node);
  bar.dataset.io = which;
  el('i', '', bar);
  node.addEventListener('click', () => showIo(which));
  paintIoNode(node);
  return node;
};

function paintIoNode (node) {
  const which = node.dataset.io;
  node.dataset.selected = cardMode === which ? 'true' : 'false';
  if (which === 'in') {
    // Der IN-Regler bekommt bei Stummschaltung eine rote Kontur und heißt
    // MUTED.
    const muted = paramOn('mute');
    node.dataset.muted = muted ? 'true' : 'false';
    node.querySelector('.lane-io-name').textContent = muted ? 'MUTED' : 'IN';
  }
}

function paintIo () { for (const n of document.querySelectorAll('#chain-panel .lane-io[data-io]')) paintIoNode(n); }
const wire = (parent, cls = '') => el('div', `lane-wire ${cls}`, parent);

/// Zellen je Spur, vom Programm (limits.laneSlots).
const laneCells = () => store.limits?.laneSlots || 12;

/// Der +-Slot fügt an pos ein und nimmt Gezogenes an derselben Stelle an. Er
/// steht in jeder freien Zelle, nicht nur am Ende.
function addTile (parent, lane, pos, section, compact = false) {
  const add = el('button', `tile tile-add${compact ? ' tile-add-compact' : ''}`, parent);
  add.title = section === 'head' ? 'Add a slot before the split'
            : section === 'tail' ? 'Add a slot after the merge. It plays for both lanes'
            : `Add a slot at ${laneLetter(lane)}${pos + 1}`;
  if (!compact) el('div', 'tile-num', add).textContent = `${laneLetter(lane)}${pos + 1}`;
  el('div', 'tile-mark', add).appendChild(icon('plus'));
  add.addEventListener('click', () => {
    const r = add.getBoundingClientRect();
    showMenu(r.left, r.bottom + 6, typeItems((type, role) =>
      structuralKeep('slot.add', { type, role, lane, pos }, null, lane === 0 ? section : null)
        .then((d) => { if (d?.slot !== undefined) selectSlot(d.slot); })));
  });
  dropTarget(add, () => ({ lane, pos, section }), 'end');
  if (lane === 0) markerTarget(add, () => pos);
  return add;
}

/// Eine Zelle: der Slot an dieser Stelle oder ein +.
function cell (parent, lane, pos, section = null) {
  const n = (store.lanes[lane] || [])[pos];
  if (n === undefined || n < 0) return addTile(parent, lane, pos, section || 'par');
  const t = slotTile(n, lane, pos, section);
  parent.appendChild(t);
  return t;
}

/// Eine Spur ohne DUAL: Eingang, alle Zellen, Ausgang.
function renderLane (parent, lane) {
  const row = el('div', 'lane', parent);
  row.dataset.lane = String(lane);
  // Runder Eingang vorn, Ausgang hinten, dazwischen das Kabel.
  ioNode(row, 'IN', 'Input');
  wire(row);
  for (let pos = 0; pos < laneCells(); ++pos) {
    if (pos > 0) wire(row);
    cell(row, lane, pos);
  }
  wire(row, 'lane-wire-tail');
  ioNode(row, 'OUT', 'Output');
}

/// DUAL: Kopf, Abzweig, zwei Spuren, Zusammenführen, Tail. Ganz parallel
/// (PARALLEL, STEREO) bleiben Kopf und Tail leer, die Marken stehen fest an
/// Eingang und Ausgang.
function renderRoute (parent) {
  const r = routeShape();
  const order = store.lanes[0];
  const stereo = Math.round(paramValue('lane_b_source')) === 2;
  const route = el('div', 'route', parent);

  // Kopf
  const head = el('div', 'route-run route-head', route);
  markerTarget(ioNode(head, 'IN', stereo ? 'Input: left to lane A, right to lane B' : 'Input'), () => 0);
  for (let p = 0; p < r.split; ++p) {
    wire(head);
    cell(head, 0, p, 'head');
  }
  if (r.splitMode) { wire(head); addTile(head, 0, r.split, 'head', true); }
  wire(head, 'route-stub');

  junction(route, 'split', r);

  // Die beiden Spuren
  const rows = el('div', 'route-rows', route);
  for (const lane of [0, 1]) {
    const row = el('div', 'lane', rows);
    row.dataset.lane = String(lane);
    wire(row, 'route-stub');
    ioNode(row, laneLetter(lane), `Lane ${laneLetter(lane)}`, 'lane-tag');
    const from = lane === 0 ? r.split : 0;
    const used = lane === 0 ? r.merge : store.lanes[1].length;
    // Ganz parallel hat jede Spur alle Zellen. Mit SPLIT und Tail teilen sich
    // Kopf, Spuren und Tail die Breite, dann steht nur ein + am Ende.
    const to = r.splitMode ? Math.min(used + 1, laneCells()) : laneCells();
    for (let p = from; p < to; ++p) {
      wire(row);
      cell(row, lane, p, lane === 0 ? 'par' : null);
    }
    wire(row, 'lane-wire-tail');
  }

  junction(route, 'merge', r);

  // Tail
  const tail = el('div', 'route-run route-tail', route);
  wire(tail, 'route-stub');
  for (let p = r.merge; p < r.len; ++p) {
    cell(tail, 0, p, 'tail');
    wire(tail);
  }
  if (r.splitMode) { addTile(tail, 0, r.len, 'tail', true); wire(tail); }
  markerTarget(ioNode(tail, 'OUT', 'Output'), () => r.len);
}

/// Abzweig oder Zusammenführen: der senkrechte Strich zwischen den Spuren und
/// bei SPLIT eine Marke zum Ziehen oder Anklicken.
function junction (parent, kind, r) {
  const j = el('div', `junction junction-${kind}`, parent);
  el('div', 'junction-line', j);
  const node = el('button', 'junction-node', j);
  node.disabled = !r.splitMode;
  el('span', 'junction-label', j).textContent = kind === 'split' ? 'SPLIT' : 'MERGE';
  if (!r.splitMode) {
    node.title = kind === 'split' ? 'Both lanes start at the input' : 'Both lanes meet at the output';
    return;
  }
  node.title = kind === 'split'
    ? 'Lane B branches off here. Drag along lane A, or click'
    : 'The lanes meet here; what follows plays for both. Drag along lane A, or click';
  node.draggable = true;
  node.addEventListener('dragstart', (e) => {
    dragMarker = kind;
    j.dataset.dragging = 'true';
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', kind);
  });
  node.addEventListener('dragend', () => { dragMarker = null; j.dataset.dragging = 'false'; });
  node.addEventListener('click', () => {
    const b = node.getBoundingClientRect();
    const items = [];
    const at = kind === 'split' ? r.split : r.merge;
    for (let i = 0; i <= r.len; ++i) {
      items.push({
        label: i === 0 ? 'IN: before everything'
             : i === r.len && kind === 'merge' ? `OUT: after A${i} · no tail`
             : `after A${i} · ${slotLabel(store.lanes[0][i - 1])}`,
        checked: at === i,
        onPick: () => moveMarker(kind, i),
      });
    }
    showMenu(b.left, b.bottom + 6, items);
  });
}

let dragMarker = null;

/// Eine Marke an eine Position von Spur A setzen. Die andere weicht aus, wenn
/// sie sonst auf der falschen Seite stünde.
async function moveMarker (kind, x) {
  const r = routeShape();
  let { split, merge } = r;
  if (kind === 'split') { split = x; merge = Math.max(merge, x); }
  else                  { merge = x; split = Math.min(split, x); }
  await setRoute(split, merge, r.len);
  await refreshAll();
}

/// Nimmt eine gezogene Marke an. at(e) liefert die Zielposition.
function markerTarget (node, at) {
  node.addEventListener('dragover', (e) => {
    if (!dragMarker) return;
    e.preventDefault();
    node.dataset.marker = 'true';
  });
  node.addEventListener('dragleave', () => { delete node.dataset.marker; });
  node.addEventListener('drop', async (e) => {
    if (!dragMarker) return;
    e.preventDefault();
    delete node.dataset.marker;
    const kind = dragMarker;
    dragMarker = null;
    await moveMarker(kind, at(e));
  });
}

/// Bild oben auf der Kachel: bei einer Datei mit Bild (Capture, IR, Box) das
/// Bild. Ein NAM-Slot ohne Capture zeigt das NAM-Bildchen, alles andere sein
/// gezeichnetes Gehäuse.
function tilePicture (n, s) {
  const mark = el('div', 'tile-mark');
  const loaded = s.file && !s.file.empty;
  if (loaded && s.file.image) {
    mark.dataset.image = 'true';
    mark.style.backgroundImage = `url("${s.file.image}")`;
  } else {
    mark.appendChild(slotArt(s.type, s.type === 'NAM' && !loaded ? null : s.role, slotColour(n)));
  }
  return mark;
}

function slotTile (n, lane, pos, section = null) {
  const s = store.slots[n];
  const t = el('button', 'tile');
  t.dataset.slot = String(n);
  t.style.setProperty('--tile-colour', slotColour(n));
  t.dataset.on = slotEnabled(n) ? 'true' : 'false';
  t.dataset.selected = selected === n ? 'true' : 'false';
  el('div', 'tile-num', t).textContent = `${laneLetter(lane)}${pos + 1}`;
  t.appendChild(tilePicture(n, s));
  // Vorschau, noch nicht gespeichert: soll auf dem Brett erkennbar sein.
  if (s.file?.preview) {
    el('div', 'tile-preview', t).textContent = 'PREVIEW';
    t.dataset.preview = 'true';
  }
  el('div', 'tile-label', t).textContent = slotLabel(n);
  t.title = s.file && !s.file.empty ? `${slotLabel(n)}: ${s.file.bank}` : slotLabel(n);

  t.addEventListener('click', () => selectSlot(n));
  // Doppelklick schaltet an und aus.
  t.addEventListener('dblclick', async () => { await setSlotEnabled(n, !slotEnabled(n)); notify('param'); });

  // Ziehen ordnet um, auch in die andere Spur.
  t.draggable = true;
  t.addEventListener('dragstart', (e) => {
    dragSlot = n;
    t.dataset.dragging = 'true';
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', String(n));
  });
  t.addEventListener('dragend', () => { dragSlot = -1; t.dataset.dragging = 'false'; });
  dropTarget(t, () => ({ lane, pos, section }), 'before');
  if (lane === 0) {
    // Eine Marke fällt vor oder hinter die Kachel, je nach Hälfte.
    markerTarget(t, (e) => {
      const b = t.getBoundingClientRect();
      return e.clientX < b.left + b.width / 2 ? pos : pos + 1;
    });
    t.addEventListener('dragover', (e) => {
      if (!dragMarker) return;
      const b = t.getBoundingClientRect();
      t.dataset.marker = e.clientX < b.left + b.width / 2 ? 'before' : 'after';
    });
  }
  return t;
}

function dropTarget (node, where, mark) {
  node.addEventListener('dragover', (e) => {
    if (dragSlot < 0) return;
    e.preventDefault();
    node.dataset.drop = mark;
  });
  node.addEventListener('dragleave', () => { delete node.dataset.drop; });
  node.addEventListener('drop', async (e) => {
    e.preventDefault();
    delete node.dataset.drop;
    if (dragSlot < 0) return;
    const n = dragSlot;
    dragSlot = -1;
    const { lane, pos, section } = where();
    // Die Zellen bleiben stehen: was weggeht, hinterlässt eine Lücke, nichts
    // rückt nach. Das Ziel ist also genau die Zelle.
    const from = store.slots[n];
    if (from.lane === lane && from.pos === pos) return;
    await structural('slot.move', { slot: n, lane, pos });
  });
}

/// Typen für das +-Menü und das Typ-Menü. Volle Typen stehen gedimmt mit Grund
/// da, die Grenzen kommen vom Programm (types[].limit).
function typeItems (pick, current = null) {
  // Ein Eintrag für alles, was ein Capture ist. Ob es Amp, Pedal, Outboard oder
  // AMP+CAB wird, bringt die geladene Datei mit (slot.load). Bis dahin zeigt
  // der Slot das NAM-Bildchen.
  const item = (t, label, role) => ({
    label,
    picture: slotArt(t.key, t.key === 'NAM' ? null : role, t.colour || typeColour(t.key), 28),
    hint: `${t.used}/${t.limit}`,
    checked: current ? current.type === t.key : undefined,
    disabled: t.used >= t.limit && !(current && current.type === t.key),
    title: `${t.limit} ${label} slots are the most the rig can run`,
    onPick: () => pick(t.key, t.key === 'NAM' && current?.type === 'NAM' ? current.role : 'amp'),
  });
  const nam = store.types.find((t) => t.key === 'NAM');
  const ir = store.types.find((t) => t.key === 'IR');
  const items = [];
  if (nam) items.push(item(nam, 'NAM CAPTURE'));
  if (ir) items.push(item(ir, 'IR · CAB'));
  items.push({ heading: 'NAMPLIFY EFFECTS' });
  for (const t of store.types) if (t !== nam && t !== ir) items.push(item(t, t.name));
  return items;
}

const typeColour = (key) => store.types.find((t) => t.key === key)?.colour || '#888888';

// Mischer

function renderMixer (parent) {
  const mixer = el('div', 'mixer', parent);
  for (const [lane, p] of [[0, 'lane_a'], [1, 'lane_b']]) {
    const strip = el('div', 'mixer-strip', mixer);
    el('div', 'mixer-strip-name', strip).textContent = laneLetter(lane);
    const knobs = el('div', 'mixer-knobs', strip);
    for (const [id, label] of [[`${p}_level`, 'LEVEL'], [`${p}_pan`, 'PAN']]) {
      const k = makeKnob(id, { small: true, label });
      knobs.appendChild(k);
      mixerParts.push(k);
    }
    const buttons = el('div', 'mixer-buttons', strip);
    for (const [id, text, kind, title] of [
      [`${p}_solo`, 'S', 'solo', 'Solo this lane'],
      [`${p}_invert`, 'Ø', 'phase', 'Flip the phase of this lane'],
    ]) {
      const b = el('button', 'mini-button', buttons);
      b.textContent = text;
      b.title = title;
      b.dataset.kind = kind;
      b.render = () => { b.dataset.on = paramOn(id) ? 'true' : 'false'; };
      b.addEventListener('click', async () => { await toggleParam(id); b.render(); });
      b.render();
      mixerParts.push(b);
    }
  }
}

// Karte

function cardKey () {
  const s = store.slots[selected];
  if (!s || s.type === 'EMPTY') return 'none';
  // Bild und PREVIEW gehören zum Schlüssel: das Bild kommt oft erst nach der
  // Datei, und STORE macht aus der Vorschau eine Bibliotheksdatei.
  return [selected, s.type, s.role, s.lane, s.pos, s.file?.bank, s.file?.variant,
          s.file?.image, s.file?.preview].join('|');
}

function renderCard () {
  const host = document.getElementById('slot-card');
  if (cardMode === 'switch' && !store.slots[switchSlot]?.onParam) cardMode = 'slot';
  if (cardMode === 'in' || cardMode === 'out') {
    // Mit Reglern: nicht neu bauen, solange es dieselbe Karte ist, sonst
    // verschwände der Regler beim Ziehen unter dem Zeiger.
    if (card?.key === `io:${cardMode}`) { for (const p of card.parts || []) p.render?.(); card.paint?.(); return; }
    host.innerHTML = '';
    card = { key: `io:${cardMode}`, parts: [] };
    renderIoCard(host, cardMode);
    return;
  }
  if (cardMode !== 'slot') {
    // Klein und ohne Regler, wird jedes Mal neu gebaut.
    host.innerHTML = '';
    card = { key: cardMode };
    if (cardMode === 'switch') renderSwitchCard(host, switchSlot); else renderMidiCard(host);
    return;
  }
  const key = cardKey();
  if (card && card.key === key) {
    card.paintHead?.();
    for (const p of card.parts || []) p.render?.();
    card.apply?.();
    return;
  }

  host.innerHTML = '';
  card = { key, parts: [] };

  if (key === 'none') {
    host.style.setProperty('--card-colour', 'var(--stroke)');
    el('div', 'slot-card-empty', host).textContent =
      'Nothing selected. Add a slot with + in the chain above.';
    return;
  }


  const n = selected;
  const s = store.slots[n];
  const colour = slotColour(n);
  host.style.setProperty('--card-colour', colour);

  // Kopf
  const head = el('div', 'slot-head', host);
  const power = el('button', 'power', head);
  power.title = 'On / off';
  power.addEventListener('click', async () => { await setSlotEnabled(n, !slotEnabled(n)); notify('param'); });

  const typeButton = el('button', 'type-button', head);
  el('span', '', typeButton).textContent = slotLabel(n);
  typeButton.appendChild(icon('chevronDown'));
  typeButton.title = 'Change what this slot is';
  typeButton.addEventListener('click', () => {
    const r = typeButton.getBoundingClientRect();
    showMenu(r.left, r.bottom + 6, typeItems((type, role) =>
      type === s.type ? structural('slot.role', { slot: n, role })
                      : structural('slot.type', { slot: n, type })
                          .then(() => type === 'NAM' && role !== 'amp'
                                ? structural('slot.role', { slot: n, role }) : null),
      { type: s.type, role: s.role }));
  });

  el('div', 'slot-where', head).textContent = `SLOT ${n + 1} · ${laneLetter(s.lane)}${s.pos + 1}`;
  el('div', 'spacer', head);

  const trash = el('button', 'icon-button', head);
  trash.title = 'Remove this slot';
  trash.appendChild(icon('trash'));
  trash.addEventListener('click', () =>
    structuralKeep('slot.remove', { slot: n }, s.lane === 0 ? sectionAt(s.pos) : null, null));

  card.paintHead = () => { power.dataset.on = slotEnabled(n) ? 'true' : 'false'; };
  card.paintHead();

  // Dateizeilen
  if (s.type === 'NAM' || s.type === 'IR') renderFileBlock(host, n, s);

  // Regler
  const fake = {};
  const controls = buildSlotControls(n, fake, colour);
  host.appendChild(controls);
  // Jede Gruppe so breit wie ihre Regler (rig.css .group).
  for (const g of controls.querySelectorAll('.group'))
    g.style.setProperty('--units', String(g.querySelector('.group-body')?.children.length || 1));
  card.parts = fake.parts || [];
  card.apply = fake.apply;

  if (s.type === 'NAM' || s.type === 'IR') renderFileFoot(host, s);
}

/// Unter den Reglern eines NAM- oder IR-Slots: LITE/FULL als Schalter und
/// daneben ein INFO-Feld mit den Angaben aus der Datei.
///
/// LITE/FULL gilt für das ganze Brett (engine.lite). Es steht an jedem
/// rechnenden Slot, weil man es dort sucht, wenn die CPU knapp wird.
function renderFileFoot (host, s) {
  const foot = el('div', 'card-foot', host);

  const eng = el('div', 'group card-foot-engine', foot);
  el('div', 'group-name', eng).textContent = 'ENGINE';
  const anyLite = !!store.engine?.anyLite;
  const lite = !!store.engine?.lite;
  // Eingelassener Kippschalter: LITE links, FULL rechts. FULL ist der
  // Werkszustand.
  const sw = el('div', 'toggle card-foot-toggle', eng);
  el('span', 'toggle-text toggle-text-a', sw).textContent = 'LITE';
  el('div', 'toggle-knob', el('div', 'toggle-rail', sw));
  el('span', 'toggle-text toggle-text-b', sw).textContent = 'FULL';
  sw.dataset.on = lite ? 'false' : 'true';
  sw.dataset.disabled = anyLite ? 'false' : 'true';
  sw.addEventListener('click', async () => {
    if (!anyLite) { showToast(eng.title, 'warn'); return; }
    await call('engine.lite', { lite: !lite });
    await refreshAll();
  });
  // Bringt keines der geladenen Captures eine zweite Größe mit, ist der
  // Schalter deaktiviert und sagt warum.
  eng.title = anyLite ? 'FULL runs the whole net, LITE saves CPU. Applies to the whole board.'
                      : 'None of the loaded captures carries a second net size, so there is nothing to switch.';

  // INFO als gedämpfte Zeile, die nie aus der Karte läuft (zu lang wird mit …
  // gekürzt, voller Text im Tooltip). Autor und ein Link zur Seite auf
  // TONE3000.
  const f = s.file || {};
  const m = f.model;
  const info = el('div', 'group card-foot-info', foot);
  el('div', 'group-name', info).textContent = 'INFO';
  const row = el('div', 'info-row', info);
  const facts = [];
  if (f.empty !== false) facts.push('Nothing loaded');
  else {
    if (f.preview) facts.push('Preview, not in your library yet');
    if (f.author) facts.push(`by ${f.author}`);
    if (m) {
      facts.push([m.architecture, m.sampleRate ? `${m.sampleRate / 1000} kHz` : ''].filter(Boolean).join(' '));
      facts.push(m.measured ? `measured ${m.measuredDb?.toFixed(1)} dB` : 'not measured');
    } else if (s.type === 'NAM') facts.push('Loading…');
  }
  const text = el('span', 'info-text', row);
  text.textContent = facts.filter(Boolean).join(' · ');
  text.title = text.textContent;
  if (f.page) {
    const link = el('button', 'info-link', row);
    link.textContent = 'TONE3000 ↗';
    link.title = 'Open its page on TONE3000';
    link.addEventListener('click', async () => {
      const r = await call('t3k.page', { page: f.page });
      if (r && r.ok === false) showToast(r.error || 'That did not work.', 'warn');
    });
  }
}

// IN, OUT

/// Karte des Eingangs oder Ausgangs, geöffnet über IN und OUT in der Kette.
function renderIoCard (host, which) {
  const isIn = which === 'in';
  const head = midiHead(host, 'var(--ink-dim)', isIn ? 'INPUT' : 'OUTPUT', 'RIG');
  head.parentElement.dataset.io = which;

  const body = el('div', 'io-body', host);
  const parts = [];
  const group = (name, ...knobs) => {
    const g = el('div', 'group', body);
    el('div', 'group-name', g).textContent = name;
    const b = el('div', 'group-body', g);
    for (const k of knobs) { b.appendChild(k); parts.push(k); }
    g.style.setProperty('--units', String(knobs.length || 1));
    return g;
  };

  if (isIn) {
    group('INPUT', makeKnob('in_gain', { label: 'GAIN' }));
    // Klick auf IN schaltet stumm und wieder live.
    const g = el('div', 'group', body);
    el('div', 'group-name', g).textContent = 'MUTE';
    const b = el('div', 'group-body io-mute', g);
    const mute = el('button', 'midi-button', b);
    card.paint = () => {
      const muted = paramOn('mute');
      mute.dataset.kind = muted ? 'danger' : '';
      mute.dataset.on = muted ? 'true' : 'false';
      mute.textContent = muted ? 'GO LIVE' : 'MUTE';
      paintIo();
    };
    card.paint();
    mute.addEventListener('click', async () => {
      await setParamUnit('mute', paramOn('mute') ? 0 : 1);
      notify('mute');
      card.paint?.();
    });
  } else {
    group('OUTPUT', makeKnob('out_gain', { label: 'LEVEL' }));
    // Doppelklick schaltet aus, Stereobreite ist optional.
    group('STEREO', makeKnob('spread_ms', { label: 'SPREAD', offAtMin: true }));
  }
  card.parts = parts;
}

/// Zeilen auf dem Display: bei NAM drei (Rolle, Modell, Preset). Die Rolle
/// steht oben, weil sie Farbe und Verhalten des Slots bestimmt (welche Regler,
/// ob MIX, ob eine Box mitkommt).
function renderFileBlock (host, n, s) {
  const block = el('div', 'nam-block', host);
  const img = el('div', 'nam-image', block);
  img.appendChild(slotArt(s.type, s.type === 'NAM' && (!s.file || s.file.empty) ? null : s.role, slotColour(n), 56));
  if (s.file?.image) {
    img.style.backgroundImage = `url("${s.file.image}")`;
    img.dataset.hasImage = 'true';
  }

  const rows = el('div', 'nam-rows', block);

  const row = (label, kind, text, empty, onStep, onPick) => {
    const r = el('div', 'nam-row', rows);
    r.dataset.kind = kind;
    el('div', 'nam-row-label', r).textContent = label;
    const left = el('button', 'nam-row-arrow', r);
    left.appendChild(icon('chevronLeft'));
    left.addEventListener('click', () => onStep(-1));
    const v = el('div', 'nam-row-value', r);
    v.textContent = text;
    v.dataset.empty = empty ? 'true' : 'false';
    v.addEventListener('click', () => onPick(v));
    const right = el('button', 'nam-row-arrow', r);
    right.appendChild(icon('chevronRight'));
    right.addEventListener('click', () => onStep(1));
    return r;
  };

  if (s.type === 'NAM') {
    const roles = store.roles;
    const at = roles.findIndex((r) => r.key === s.role);
    row('CATEGORY', 'role', roles[at]?.name || s.role, false,
      (dir) => structural('slot.role', { slot: n, role: roles[(at + dir + roles.length) % roles.length].key }),
      (v) => {
        const r = v.getBoundingClientRect();
        showMenu(r.left, r.bottom + 4, roles.map((x) => ({
          label: x.name, checked: x.key === s.role,
          onPick: () => structural('slot.role', { slot: n, role: x.key }),
        })));
      });
  }

  const f = s.file || { empty: true };
  const step = (line) => async (dir) => { await stepLibrary(n, line, dir); await refreshAll(); };
  const model = row('MODEL', 'bank', f.empty ? 'Choose a model…' : f.bank, f.empty,
      step('bank'), (v) => openPicker(n, 'bank', v));
  // Vorschau, noch nicht in der Bibliothek: rechts in der Zeile die Pille und
  // dahinter STORE. Die Kachel trägt dasselbe Wort.
  if (f.preview) {
    model.dataset.tag = 'true';
    const end = el('div', 'nam-row-end');
    model.insertBefore(end, model.lastElementChild);
    const tag = el('em', 'nam-row-tag', end);
    tag.textContent = 'PREVIEW';
    tag.title = 'Previewed from TONE3000, not in your library yet';
    const keep = el('button', 'nam-row-action', end);
    keep.title = 'Store it in your library';
    keep.append(icon('download'), document.createTextNode('STORE'));
    keep.addEventListener('click', async (e) => {
      e.stopPropagation();
      keep.disabled = true;
      const reply = await call('t3k.keep', { slot: n });
      if (!reply?.ok) {
        keep.disabled = false;
        showToast(reply?.error === 'busy' ? 'Something is already loading.' : (reply?.error || 'That did not work.'),
                  reply?.error === 'busy' ? 'warn' : 'error');
        return;
      }
      const { markStored } = await import('./drawer.js');
      markStored(reply.data?.tone, reply.data?.model);
      invalidateLibrary();
      await refreshAll();
    });
  }
  row('PRESET', 'preset', f.empty ? '—' : f.variant, f.empty,
      step('preset'), (v) => openPicker(n, 'preset', v));
}

// Fußschalter: einer je Slot, in Kettenreihenfolge. Sie schalten nicht selbst,
// an und aus geht per Doppelklick auf die Kachel. Die Leuchte zeigt, ob der
// Slot klingt, ein Klick lernt den MIDI-Befehl an, der ihn schaltet.

let feet = [];
let learning = null;       // onParam, der gerade auf einen MIDI-Befehl wartet
let learnTimer = null;

function assignmentFor (param) {
  return store.settings?.midiAssignments?.find((a) => a.param === param) || null;
}

function startLearn (param) {
  if (learning === param) {
    call('param.learnCancel', { id: param });
    learning = null;
  } else {
    learning = param;
    call('param.learn', { id: param });
    clearTimeout(learnTimer);
    learnTimer = setTimeout(() => { learning = null; paintFeet(); }, 8000);
  }
  paintFeet();
}

function renderFeet () {
  const panel = document.getElementById('foot-panel');
  panel.innerHTML = '';
  feet = [];
  // Vorn: alle Zuweisungen auf einen Blick, in der Mitte wie jeder Schalter.
  const all = el('button', 'foot foot-edit', panel);
  all.title = 'All MIDI assignments';
  el('div', 'foot-edit-mark', all).appendChild(icon('midi'));
  el('div', 'foot-name', all).textContent = 'MIDI';
  el('div', 'foot-cc', all).textContent = 'ALL';
  all.paint = () => { all.dataset.selected = cardMode === 'midi' ? 'true' : 'false'; };
  all.addEventListener('click', showMidi);
  feet.push(all);
  el('div', 'foot-sep', panel);

  const lanes = paramOn('dual_on') ? [0, 1] : [0];
  lanes.forEach((lane, i) => {
    if (i > 0) el('div', 'foot-sep', panel);
    for (const n of store.lanes[lane]) {
      const param = store.slots[n]?.onParam;
      const wrap = el('button', 'foot', panel);
      wrap.style.setProperty('--knob-colour', slotColour(n));
      const led = el('div', 'foot-led', wrap);
      el('div', 'foot-name', wrap).textContent = slotLabel(n);
      const cc = el('div', 'foot-cc', wrap);
      wrap.addEventListener('click', () => { if (param) showSwitch(n); });
      wrap.addEventListener('contextmenu', async (e) => {
        e.preventDefault();
        if (!param || !assignmentFor(param)) return;
        await call('param.forget', { id: param });
        await refreshAll();
      });
      wrap.paint = () => {
        const a = assignmentFor(param);
        if (learning === param && a) { learning = null; clearTimeout(learnTimer); }
        const waiting = learning === param;
        wrap.dataset.on = slotEnabled(n) ? 'true' : 'false';
        wrap.dataset.learning = waiting ? 'true' : 'false';
        wrap.dataset.assigned = a ? 'true' : 'false';
        wrap.dataset.selected = cardMode === 'switch' && switchSlot === n ? 'true' : 'false';
        cc.textContent = waiting ? 'PRESS…' : a ? `CC ${a.cc}` : 'MIDI';
        wrap.title = a ? `${slotLabel(n)}: switched by CC ${a.cc}${a.channel ? ` on channel ${a.channel}` : ''}. Click to edit`
                       : `${slotLabel(n)}: click to assign a MIDI footswitch`;
        led.dataset.on = wrap.dataset.on;
      };
      wrap.paint();
      feet.push(wrap);
    }
  });
}

function paintFeet () {
  for (const f of feet) f.paint();
  // Die offene Schalterkarte zeigt dasselbe wie die Leiste: wartet, gelernt.
  if (cardMode !== 'slot') renderCard();
}

// MIDI in der Mitte: Kopf mit Schalter, Name in der Farbe des Slots, darunter
// ein Glasfeld mit den Zeilen und die Knöpfe.

function midiHead (host, colour, title, where) {
  host.style.setProperty('--card-colour', colour);
  const head = el('div', 'slot-head', host);
  el('div', 'type-button', head).textContent = title;
  el('div', 'slot-where', head).textContent = where;
  el('div', 'spacer', head);
  return head;
}

function screenRow (screen, label, value, opts = {}) {
  const row = el('div', 'nam-row midi-row', screen);
  el('div', 'nam-row-label', row).textContent = label;
  const v = el('div', 'nam-row-value', row);
  v.textContent = value;
  if (opts.dim) v.dataset.empty = 'true';
  if (opts.accent) v.dataset.accent = 'true';
  return row;
}

function renderSwitchCard (host, n) {
  const s = store.slots[n];
  const param = s.onParam;
  const a = assignmentFor(param);
  const waiting = learning === param;
  const pos = `${laneLetter(s.lane)}${s.pos + 1}`;

  const head = midiHead(host, slotColour(n), slotLabel(n), `FOOTSWITCH · ${pos}`);
  const power = el('button', 'power', head);
  power.dataset.on = slotEnabled(n) ? 'true' : 'false';
  power.title = 'Switch the slot on or off';
  power.addEventListener('click', async () => { await setSlotEnabled(n, !slotEnabled(n)); notify('param'); });
  head.prepend(power);

  const body = el('div', 'midi-body', host);
  const screen = el('div', 'nam-rows midi-screen', body);
  screenRow(screen, 'TARGET', `${slotLabel(n)} · ON / OFF`);
  screenRow(screen, 'CONTROL', waiting ? 'Press a switch on your controller…'
                                       : a ? `CC ${a.cc}` : 'Not assigned', { dim: !a && !waiting, accent: waiting });
  screenRow(screen, 'CHANNEL', a ? (a.channel ? String(a.channel) : 'ANY') : '—', { dim: !a });

  const buttons = el('div', 'midi-buttons', body);
  const learn = el('button', 'midi-button', buttons);
  learn.dataset.kind = 'learn';
  learn.dataset.on = waiting ? 'true' : 'false';
  learn.textContent = waiting ? 'CANCEL' : a ? 'LEARN AGAIN' : 'LEARN';
  learn.title = waiting ? 'Stop waiting' : 'Then press a footswitch on your MIDI controller';
  learn.addEventListener('click', () => startLearn(param));

  if (a) {
    const forget = el('button', 'midi-button', buttons);
    forget.dataset.kind = 'danger';
    forget.textContent = 'REMOVE';
    forget.addEventListener('click', async () => { await call('param.forget', { id: param }); await refreshAll(); });
  }

  const edit = el('button', 'midi-button', buttons);
  edit.textContent = 'EDIT EFFECT';
  edit.title = 'Show the card of this slot';
  edit.addEventListener('click', () => selectSlot(n));

  el('div', 'midi-hint', body).textContent =
    'The light follows the slot: lit while it plays. On screen, double-click its tile in the chain to switch it.';
}

function renderMidiCard (host) {
  midiHead(host, 'var(--ink-dim)', 'MIDI', 'ALL ASSIGNMENTS');
  const body = el('div', 'midi-body', host);
  const list = store.settings?.midiAssignments || [];
  const screen = el('div', 'nam-rows midi-screen', body);
  if (!list.length) {
    screenRow(screen, 'NONE', 'Nothing learned yet', { dim: true });
  } else {
    for (const a of list) {
      const row = screenRow(screen, `CC ${a.cc}`, `${a.paramName}${a.channel ? ` · ch ${a.channel}` : ''}`);
      const x = el('button', 'midi-row-remove', row);
      x.title = 'Remove this assignment';
      x.appendChild(icon('trash'));
      x.addEventListener('click', async () => { await call('param.forget', { id: a.param }); await refreshAll(); });
    }
  }
  const buttons = el('div', 'midi-buttons', body);
  if (list.length) {
    const clear = el('button', 'midi-button', buttons);
    clear.dataset.kind = 'danger';
    clear.textContent = 'CLEAR ALL';
    clear.addEventListener('click', async () => {
      const ok = await showDialog({ title: 'CLEAR ALL', text: 'Remove every MIDI assignment?',
                                    confirm: 'CLEAR ALL', danger: true });
      if (!ok) return;
      await call('settings.midiClear');
      await refreshAll();
    });
  }
  el('div', 'midi-hint', body).textContent =
    'Footswitches: click one in the bar below. Knobs: right-click the knob, then move a control on your controller. '
    + 'Channel, program change and thru are in Settings › MIDI.';
}
