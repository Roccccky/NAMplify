// Demo-Backend für den Browser: die UI lässt sich ohne Programm ansehen und
// bedienen. Alle Werte sind erfunden. Nachgebildet wird nur die Form der
// Antworten, samt Wartezeiten, leeren Listen, Fehlern und Fortschritt.

// Parameter
//
// Regler, Typen, Rollen, Grenzen und Farben kommen aus dem Kern (mock-defs.js,
// erzeugt von tests/MockDefsTest.cpp).
import { LIMITS, FIXED, TYPES, ROLES, TOOLS, DRIVE_DEFAULTS, REVERB_DEFAULTS,
         DEFAULT_LAYOUT } from './mock-defs.js';

const L = 'linear', G = 'log', B = 'bool', C = 'choice';

/// Die ganze Parameterliste wie im Programm: erst die festen, dann je Slot
/// s{n}_on und s{n}_k1 … k11 (0…1).
const paramDefs = [];
for (const d of FIXED) paramDefs.push({ ...d, index: paramDefs.length });
for (let s = 0; s < LIMITS.slots; ++s) {
  paramDefs.push({ index: paramDefs.length, id: `s${s + 1}_on`, name: `Slot ${s + 1}`,
                   kind: B, min: 0, max: 1, def: 0, unit: '', slot: s, k: -1 });
  for (let k = 0; k < LIMITS.slotControls; ++k)
    paramDefs.push({ index: paramDefs.length, id: `s${s + 1}_k${k + 1}`,
                     name: `Slot ${s + 1} Knob ${k + 1}`, kind: L, min: 0, max: 1, def: 0,
                     unit: '', slot: s, k });
}

const defById = new Map(paramDefs.map((d) => [d.id, d]));
const typeByKey = new Map(TYPES.map((t) => [t.key, t]));

// Dieselben Abbildungen wie ParamDesc im Kern.
function toUnit (d, v) {
  if (d.kind === B) return v >= .5 ? 1 : 0;
  if (d.kind === C) return d.max > 0 ? v / d.max : 0;
  if (d.kind === G) return Math.log(Math.max(d.min, v) / d.min) / Math.log(d.max / d.min);
  return (v - d.min) / (d.max - d.min);
}
function fromUnit (d, u) {
  u = Math.min(1, Math.max(0, u));
  if (d.kind === B) return u >= .5 ? 1 : 0;
  if (d.kind === C) return Math.round(u * d.max);
  if (d.kind === G) return d.min * Math.pow(d.max / d.min, u);
  return d.min + (d.max - d.min) * u;
}
function textOf (d, v) {
  if (d.kind === B) return v >= .5 ? 'ON' : 'OFF';
  if (d.kind === C) return d.choices[Math.round(v)] ?? '';
  const span = d.max - d.min;
  const s = span >= 100 ? v.toFixed(0) : span >= 5 ? v.toFixed(1) : v.toFixed(2);
  return d.unit ? `${s} ${d.unit}` : s;
}

// Zustand
const state = {
  params: Object.fromEntries(paramDefs.map((d) => [d.id, toUnit(d, d.def)])),
  // Aufbau: Typ und Rolle je Slot, die Spuren als Slotlisten.
  layout: {
    type: Array(LIMITS.slots).fill('EMPTY'),
    role: Array(LIMITS.slots).fill('amp'),
    lanes: [[], []],
  },
  tools: Object.fromEntries(TOOLS.map((t) => [t.key, false])),
  zoom: 2,
  structure: 0,
  // path ist das, was sich der Slot merkt. Das Bild kommt daraus (siehe
  // imageForPath), wie im Programm.
  files: Array.from({ length: LIMITS.slots }, () => null),
  presets: { list: ['Bedroom Clean', 'Crunch Rhythm', 'Lead Boost'], current: '' },
  t3k: { hasKey: true, connected: false, user: '' },
  audio: { inputChannels: [0] },
  settings: {
    // Wie ausgeliefert: die Startabfrage erscheint. Zum Entwickeln
    // vorübergehend 2 (= GO LIVE). tools/verify-release.mjs verlangt 0.
    onStart: 0, tunerDisplay: 0, tunerMute: true, interfaceDbu: 12,
    engineLite: false, captureOversampling: 1, eqEnabled: false, eqCurveName: 'FLAT',
    eqCurves: [
      { name: 'FLAT', description: 'All bands at 0 dB.', factory: true },
      { name: 'GUITAR CUT', description: 'Rumble off the bottom, hiss off the top.', factory: true },
      { name: 'SCOOP', description: 'Mids scooped, for distorted rhythm tracks.', factory: true },
      { name: 'LEAD', description: 'Mids forward, so a solo sits above the rest.', factory: true },
      { name: 'CLEAN UP', description: 'Just the edges off, nothing else.', factory: true },
    ],
    eqBands: [
      { freq: 80, gain: 0, q: 1, shape: 0, shapeName: 'BELL' },
      { freq: 200, gain: 0, q: 1, shape: 0, shapeName: 'BELL' },
      { freq: 500, gain: 0, q: 1, shape: 0, shapeName: 'BELL' },
      { freq: 1200, gain: 0, q: 1, shape: 0, shapeName: 'BELL' },
      { freq: 3000, gain: 0, q: 1, shape: 0, shapeName: 'BELL' },
      { freq: 6000, gain: 0, q: 1, shape: 0, shapeName: 'BELL' },
      { freq: 10000, gain: 0, q: 1, shape: 0, shapeName: 'BELL' },
    ],
    midiAssignments: [],
    libraryPath: '~/Documents/NAMplify',
    libraryFolderName: 'NAMplify',
    version: '2.0.0-beta.2 (dry run)',
  },
  looper: Array.from({ length: 4 }, () => ({ state: 'EMPTY', pos: 0, vol: .8, seconds: 0, beats: 0, rawLength: 0, loopStart: 0, loopEnd: 0 })),
  recorder: { state: 0, pos: 0, seconds: 0, has: false, full: false, l: 0, r: 0 },
  busy: false,
};

const ENGINE = {
  sampleRate: 48000, blockSize: 128, latency: 0, lite: false, anyLite: true, standalone: true,
  driveDefaults: DRIVE_DEFAULTS,
  reverbDefaults: REVERB_DEFAULTS,
};

// Erfundene Bilder, aus dem Namen erzeugt, damit dieselbe Zeile immer dasselbe
// Bild bekommt. Damit lassen sich Listen mit Bildern und das Nachladen beim
// Scrollen testen.
function mockImage (seed) {
  let h = 0;
  for (let i = 0; i < seed.length; ++i) h = (h * 31 + seed.charCodeAt(i)) >>> 0;
  const hue = h % 360;
  const initials = seed.replace(/[^A-Za-z0-9 ]/g, ' ').trim().split(/\s+/)
                       .slice(0, 2).map((w) => w[0].toUpperCase()).join('');
  const svg =
    `<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96">`
  + `<rect width="96" height="96" fill="hsl(${hue} 34% 26%)"/>`
  + `<circle cx="48" cy="40" r="26" fill="hsl(${(hue + 40) % 360} 46% 44%)"/>`
  + `<text x="48" y="52" font-family="Helvetica,Arial" font-size="26" font-weight="700"`
  + ` fill="hsl(${hue} 20% 92%)" text-anchor="middle">${initials}</text>`
  + `</svg>`;
  return 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
}

// Erfundene Bibliothek
/// Bild der Bank, in der diese Datei liegt, nachgeschlagen im Bestand wie im
/// Programm.
function bankForPath (path) {
  if (!path) return null;
  const dir = String(path).replace(/\/[^/]*$/, '');
  for (const list of Object.values(LIB))
    for (const bank of list)
      if (bank.path === dir) return bank;
  return null;
}
const imageForPath = (path) => bankForPath(path)?.image || '';

/// Autor und Ort, wie Library::bankForFile.
function bankInfo (path) {
  const b = bankForPath(path);
  if (!b) return {};
  return { author: b.author, toneId: b.toneId,
           page: b.toneId > 0 ? `https://www.tone3000.com/tones/${b.title.toLowerCase().replace(/[^a-z0-9]+/g, '-')}-${b.toneId}` : '' };
}

const LIB = {
  AMP: [
    { title: 'Tweed Combo', author: 'mock', gear: 'amp', toneId: 9001, image: mockImage('Tweed Combo'), path: 'NAM/Models/Tweed.namplify',
      variants: [{ name: 'Edge of Break-up', size: 'standard', path: 'NAM/Models/Tweed.namplify/edge.nam', modelId: 1 },
                 { name: 'Cranked', size: 'standard', path: 'NAM/Models/Tweed.namplify/cranked.nam', modelId: 2 },
                 { name: 'Clean', size: 'lite', path: 'NAM/Models/Tweed.namplify/clean.nam', modelId: 3 }] },
    { title: 'British Lead 100', author: 'mock', gear: 'amp', toneId: 9002, image: mockImage('British Lead 100'), path: 'NAM/Models/Brit.namplify',
      variants: [{ name: 'Input I', size: 'standard', path: 'NAM/Models/Brit.namplify/i.nam', modelId: 4 },
                 { name: 'Jumped', size: 'standard', path: 'NAM/Models/Brit.namplify/j.nam', modelId: 5 }] },
    { title: 'Modern High Gain (Fullrig)', author: 'mock', gear: 'amp-cab', toneId: 9003, image: mockImage('Modern High Gain (Fullrig)'), path: 'NAM/Models/Modern.namplify',
      variants: [{ name: 'Rhythm', size: 'standard', path: 'NAM/Models/Modern.namplify/r.nam', modelId: 6 }] },
  ],
  CAB: [
    { title: 'Vintage Combo 2x12', author: 'NAMplify', gear: 'cab', toneId: -101, image: mockImage('Vintage Combo 2x12'), path: 'NAM/IRs/Vintage.namplify',
      variants: [{ name: 'Standard', size: 'standard', path: 'NAM/IRs/Vintage.namplify/v.wav', modelId: 101 }] },
    { title: 'British Stack 4x12', author: 'NAMplify', gear: 'cab', toneId: -102, image: mockImage('British Stack 4x12'), path: 'NAM/IRs/British.namplify',
      variants: [{ name: 'Standard', size: 'standard', path: 'NAM/IRs/British.namplify/b.wav', modelId: 102 }] },
    { title: 'Modern Tight 4x12', author: 'NAMplify', gear: 'cab', toneId: -103, image: mockImage('Modern Tight 4x12'), path: 'NAM/IRs/Modern.namplify',
      variants: [{ name: 'Standard', size: 'standard', path: 'NAM/IRs/Modern.namplify/m.wav', modelId: 103 }] },
  ],
  PEDAL: [
    { title: 'Green Screamer', author: 'mock', gear: 'pedal', toneId: 9101, image: mockImage('Green Screamer'), path: 'NAM/Pedals/Green.namplify',
      variants: [{ name: 'Drive 9 o’clock', size: 'standard', path: 'NAM/Pedals/Green.namplify/a.nam', modelId: 201 },
                 { name: 'Drive noon', size: 'standard', path: 'NAM/Pedals/Green.namplify/b.nam', modelId: 202 }] },
  ],
  SPACE: [
    { title: 'Studio Live Room', author: 'mock', gear: 'space', toneId: 9301, image: mockImage('Studio Live Room'), path: 'NAM/Outboard/Room.namplify',
      variants: [{ name: 'Close', size: 'standard', path: 'NAM/Outboard/Room.namplify/close.nam', modelId: 401 }] },
  ],
  OUTBOARD: [
    { title: 'Tape Machine', author: 'mock', gear: 'outboard', toneId: 9201, image: mockImage('Tape Machine'), path: 'NAM/Outboard/Tape.namplify',
      variants: [{ name: '15 ips', size: 'standard', path: 'NAM/Outboard/Tape.namplify/15.nam', modelId: 301 }] },
  ],
};

// Erfundene Suche
const T3K_TITLES = [
  ['Blackface Twin', 'amp', 'jdoe', 24], ['British 800 Lead', 'amp', 'ampnerd', 61],
  ['Rectified Modern', 'amp-cab', 'metalcap', 118], ['Top Boost 30', 'amp', 'britcombo', 44],
  ['Gold Horse Overdrive', 'pedal', 'goldpanda', 210], ['Green Mid Drive', 'pedal', 'greenmachine', 305],
  ['Plate 140', 'space', 'roomtone', 33], ['Cathedral', 'space', 'roomtone', 12],
  ['FET Compressor Blue', 'outboard', 'studiocat', 18], ['Tube Program EQ', 'outboard', 'studiocat', 26],
  ['V30 4x12 Off-axis', 'cab', 'irlab', 402], ['Greenback 2x12', 'cab', 'irlab', 271],
  ['Tweed 4x10', 'amp', 'tweedhead', 33], ['Overdrive Special', 'amp-cab', 'boutique', 88],
  ['Germanium Fuzz', 'pedal', 'fuzzlord', 57], ['Tape Echo 201', 'outboard', 'tapehead', 40],
  ['Tweed Tiny 5W', 'amp', 'tweedhead', 51], ['Orange Box 120', 'amp', 'britcombo', 29],
  ['Big Ram Fuzz', 'pedal', 'fuzzlord', 96], ['Opto Leveler', 'outboard', 'studiocat', 64],
  ['British 4x12 Basket', 'cab', 'irlab', 188], ['Top Boost 2x12', 'cab', 'irlab', 143],
  ['Boutique 100 Lead', 'amp', 'metalcap', 77], ['Hot Rod 100', 'amp-cab', 'boutique', 92],
];

let t3kStore = null;
function buildT3k () {
  if (t3kStore) return t3kStore;
  t3kStore = [];
  for (let i = 0; i < 96; ++i) {
    const [base, gear, author, downloads] = T3K_TITLES[i % T3K_TITLES.length];
    const id = 1000 + i;
    t3kStore.push({
      id,
      title: i < T3K_TITLES.length ? base : `${base} #${1 + Math.floor(i / T3K_TITLES.length)}`,
      gear,
      format: gear === 'cab' ? 'ir' : 'nam',
      author,
      // Adresse statt Bild, damit es durch die Warteschlange läuft wie beim
      // Dienst.
      image: `mock://image/${id}`,
      models: 1 + (i % 5),
      downloads: downloads + i,
      favorites: (i * 7) % 40,
      isFavorite: i % 11 === 0,
      sizes: gear === 'cab' ? [] : (i % 3 === 0 ? ['standard', 'lite'] : ['standard']),
      url: '',
      type: gear === 'cab' ? 'IR' : 'NAM',
      role: roleForGear(gear),
      here: false,
    });
  }
  return t3kStore;
}

const CATEGORY_GEAR = [null, 'amp', 'amp-cab', 'cab', 'pedal', 'outboard', 'space'];

// Meter
let emit = () => {};
let t = 0;

let lastStructure = -1;

export function mockStart (emitter) {
  // Ereignisse gehen als Kopie hinaus. Im Programm laufen sie durch JSON
  // (WebBridge.cpp, __juce__invoke), die UI bekommt also immer frische Objekte.
  // Ohne Kopie würde ein Vergleich auf Änderungen in der Demo nie anschlagen.
  emit = (event, payload) => emitter(event, structuredClone(payload));
  setInterval(() => {
    t += 1 / 30;

    // Wie im Programm: Strukturänderungen werden aus einer Schleife gemeldet,
    // nicht von jedem Befehl einzeln.
    if (state.structure !== lastStructure) {
      lastStructure = state.structure;
      emit('structure', state.structure);
    }

    // Positionszeiger für Looper und Recorder. Im Programm kommt er aus dem
    // Kern (positionUnit), hier wird er nachgestellt.
    for (const tr of state.looper) {
      if (tr.state === 'PLAYING' && tr.seconds > 0)
        tr.pos = (tr.pos + (1 / 30) / tr.seconds) % 1;
      else if (tr.state === 'EMPTY') tr.pos = 0;
    }
    if (state.recorder.seconds > 0) {
      if (state.recorder.state === 2)      // spielt ab
        state.recorder.pos = (state.recorder.pos + (1 / 30) / state.recorder.seconds) % 1;
      else if (state.recorder.state === 1) // nimmt auf, der Zeiger steht am Ende
        state.recorder.pos = 1;
    }

    const playing = (Math.sin(t * .7) * .5 + .5);
    const level = state.params.mute >= .5 ? 0 : playing * (.35 + .3 * Math.sin(t * 5.3));

    const flashes = Array.from({ length: 16 }, (_, i) => {
      const step = Math.floor((t * (state.params.drums_on >= .5 ? 6 : 0)) % 16);
      const d = (i - step + 16) % 16;
      return d === 0 ? (i === 0 ? 1 : i % 4 === 0 ? .8 : .42) : Math.max(0, .4 - d * .12);
    });

    const cents = Math.sin(t * .6) * 34;
    emit('meters', {
      in: Math.max(0, level), out: Math.max(0, level * .9),
      cpu: .18 + .06 * Math.sin(t * .4),
      muted: state.params.mute >= .5,
      latency: 0,
      tuner: { has: true, freq: 110 * Math.pow(2, cents / 1200), cents,
               note: 'A', octave: 2, confidence: .9 },
      drums: { step: Math.floor((t * 6) % 16), flashes, bpm: fromUnit(defById.get('drums_bpm'), state.params.drums_bpm) },
      looper: state.looper,
      looperRecording: state.looper.findIndex((x) => x.state === 'RECORDING'),
      looperOldest: state.looper.findIndex((x) => x.state === 'PLAYING'),
      recorder: state.recorder,
      clock: { source: 'none', running: false, bpm: 120 },
    });
  }, 33);
}

const delay = (ms) => new Promise((r) => setTimeout(r, ms));

// Slots. Dieselben Regeln wie NamplifyProcessor
// (plugin/src/PluginProcessor.cpp): Grenzen je Typ, Werkswerte beim Anlegen,
// ein Amp holt eine IR dahinter, Full Rig schaltet sie aus, Looper holt Drums.

/// Was ein Parameter gerade bedeutet: bei einem Slot-Regler der Regler des
/// Typs, der dort liegt (effectiveDesc).
function effDef (id) {
  const d = defById.get(id);
  if (!d || d.k === undefined || d.k < 0) return d;
  const type = typeByKey.get(state.layout.type[d.slot]);
  return type?.controls[d.k] || d;
}

const pid = (slot, k) => k < 0 ? `s${slot + 1}_on` : `s${slot + 1}_k${k + 1}`;
const ctlIndex = (typeKey, id) => typeByKey.get(typeKey).controls.findIndex((c) => c.id === id);

function setSlotValue (slot, id, value) {
  const typeKey = state.layout.type[slot];
  const k = ctlIndex(typeKey, id);
  if (k < 0) return;
  state.params[pid(slot, k)] = toUnit(typeByKey.get(typeKey).controls[k], value);
}

function resetSlotControls (slot) {
  const type = typeByKey.get(state.layout.type[slot]);
  for (let k = 0; k < LIMITS.slotControls; ++k) {
    const c = type?.controls[k];
    state.params[pid(slot, k)] = c ? toUnit(c, c.def) : 0;
  }
  state.params[pid(slot, -1)] = type ? 1 : 0;
}

function findSlot (slot) {
  for (let lane = 0; lane < LIMITS.lanes; ++lane) {
    const pos = state.layout.lanes[lane].indexOf(slot);
    if (pos >= 0) return { lane, pos };
  }
  return null;
}

const countType = (key) => state.layout.type.filter((t) => t === key).length;

function irAfter (slot) {
  const at = findSlot(slot);
  if (!at) return -1;
  const lane = state.layout.lanes[at.lane];
  for (let p = at.pos + 1; p < lane.length; ++p)
    if (state.layout.type[lane[p]] === 'IR') return lane[p];
  return -1;
}

// Spuren mit Lücken wie im Programm (Layout::insertAt/removeAt): -1 ist eine
// freie Zelle, am Ende wird nichts Leeres gehalten.
function trimLane (row) { while (row.length && row[row.length - 1] < 0) row.pop(); }

function insertAt (lane, pos, slot) {
  const row = state.layout.lanes[lane];
  pos = Math.max(0, Math.min(LIMITS.laneSlots - 1, pos));
  while (row.length < pos) row.push(-1);
  if (pos >= row.length || row[pos] < 0) { row[pos] = slot; return true; }
  let gap = row.indexOf(-1, pos);
  if (gap < 0) { if (row.length >= LIMITS.laneSlots) return false; gap = row.length; row.push(-1); }
  row.splice(gap, 1);
  row.splice(pos, 0, slot);
  return true;
}

function removeAt (lane, pos) {
  const row = state.layout.lanes[lane];
  row[pos] = -1;
  trimLane(row);
}

function notice (text, kind = 'info') { emit('notice', { text, kind }); }

function addSlot (typeKey, role, lane, pos, announce = true) {
  const type = typeByKey.get(typeKey);
  if (!type) return -1;
  if (countType(typeKey) >= type.limit) {
    if (announce) notice(`${type.limit} ${type.name} slots are the most the rig can run`, 'warn');
    return -1;
  }
  const slot = state.layout.type.indexOf('EMPTY');
  if (slot < 0) { if (announce) notice('All slots are in use', 'warn'); return -1; }
  if (!insertAt(lane, pos, slot)) { if (announce) notice('That lane is full', 'warn'); return -1; }

  state.layout.type[slot] = typeKey;
  state.layout.role[slot] = role || 'amp';
  state.files[slot] = null;
  resetSlotControls(slot);
  // Keine Box dazu: die Rolle des Slots steht noch nicht fest.
  state.structure++;
  return slot;
}

function removeSlot (slot) {
  const at = findSlot(slot);
  if (!at) return;
  removeAt(at.lane, at.pos);
  state.layout.type[slot] = 'EMPTY';
  state.files[slot] = null;
  state.params[pid(slot, -1)] = 0;
  state.structure++;
}

function ensureIrAfter (slot, announce) {
  let ir = irAfter(slot);
  if (ir < 0) {
    const at = findSlot(slot);
    ir = addSlot('IR', 'amp', at.lane, at.pos + 1, false);
    if (ir < 0) return;
    if (announce) notice('IR came along with AMP');
  }
  if (!state.files[ir]) {
    const bank = LIB.CAB[0];
    state.files[ir] = { bank: bank.title, variant: bank.variants[0].name, gear: 'cab',
                        path: bank.variants[0].path };
  }
  state.params[pid(ir, -1)] = state.files[ir] ? 1 : 0;
}

const roleForGear = (g) => g === 'pedal' ? 'pedal' : g === 'outboard' ? 'outboard' : g === 'space' ? 'space'
                         : (g === 'amp-cab' || g === 'full-rig') ? 'fullrig' : 'amp';

/// Vorschau-Capture in die Bibliothek legen, ohne es neu zu laden (wie
/// WebBridge::keepPreview).
function keepPreview (slot) {
  const f = state.files[slot];
  if (!f?.preview) return null;
  downloaded.add(f.tone); downloadedModels.add(f.model);
  state.files[slot] = { ...f, preview: false };
  return { slot, tone: f.tone, model: f.model };
}

function loadIntoSlot (slot, file) {
  const typeKey = state.layout.type[slot];
  if (typeKey !== 'NAM' && typeKey !== 'IR') return;
  const sameBank = state.files[slot]?.bank === file.bank;
  if (typeKey === 'NAM' && file.gear) state.layout.role[slot] = roleForGear(file.gear);
  state.files[slot] = file;
  state.params[pid(slot, -1)] = 1;

  if (typeKey === 'IR') { if (!sameBank) setSlotValue(slot, 'cab_output', 0); }
  else {
    setSlotValue(slot, 'nam_gain', 0);
    if (!sameBank) for (const [id, v] of [['nam_level', 0], ['nam_mix', 1], ['amp_bass', 0],
                                          ['amp_mid', 0], ['amp_treble', 0]]) setSlotValue(slot, id, v);
    const role = state.layout.role[slot];
    if (role === 'fullrig') { const ir = irAfter(slot); if (ir >= 0) state.params[pid(ir, -1)] = 0; }
    else if (role === 'amp') ensureIrAfter(slot, true);
  }
  state.structure++;
}

// AMP und FULL RIG teilen sich einen Slot, wie nmp::sharesPlace.
const sharesPlace = (a, b) => a === b || (['amp', 'fullrig'].includes(a) && ['amp', 'fullrig'].includes(b));

function slotForFile (typeKey, role, preferred) {
  // Wie im Programm (nmp::placeForFile): ein Pedal überschreibt keinen Amp, ein
  // Full Rig ersetzt den Amp und umgekehrt.
  const fits = (s) => s >= 0 && s < LIMITS.slots && state.layout.type[s] === typeKey
    && (typeKey !== 'NAM' || !state.files[s] || sharesPlace(state.layout.role[s], role));
  if (fits(preferred)) return preferred;
  const sameRole = (s) => typeKey !== 'NAM' || sharesPlace(state.layout.role[s], role);
  for (const pass of [0, 1])
    for (const lane of state.layout.lanes)
      for (const s of lane)
        if (state.layout.type[s] === typeKey && sameRole(s) && (pass === 1 || !state.files[s])) return s;
  const row = state.layout.lanes[0];
  let pos = row.length;
  if (typeKey === 'NAM' && role !== 'outboard' && role !== 'space') {
    const i = row.findIndex((s) => state.layout.type[s] === 'IR');
    if (i >= 0) pos = i;
  }
  if (typeKey === 'NAM' && role === 'pedal') {
    const i = row.findIndex((s) => s >= 0 && state.layout.type[s] === 'NAM'
                                   && !['pedal', 'outboard', 'space'].includes(state.layout.role[s]));
    if (i >= 0) pos = i > 0 && row[i - 1] < 0 ? i - 1 : i;
  }
  // Leere Kette: der Amp in die Mitte, davor Platz für Pedale.
  if (row.length === 0)
    pos = typeKey === 'NAM' && role === 'pedal' ? 2
        : typeKey === 'NAM' && (role === 'outboard' || role === 'space') ? Math.floor(LIMITS.laneSlots * 2 / 3)
        : typeKey === 'NAM' ? Math.floor(LIMITS.laneSlots / 3) : Math.floor(LIMITS.laneSlots / 2);
  return addSlot(typeKey, role, 0, pos, true);
}

function paramSnapshot () {
  const out = {};
  for (const base of paramDefs) {
    const d = effDef(base.id);
    const unit = state.params[base.id];
    const value = fromUnit(d, unit);
    out[base.id] = { unit, value, text: textOf(d, value) };
  }
  return out;
}

const MODEL_INFO = { architecture: 'WaveNet', sampleRate: 48000, modelRate: 48000, resampling: false,
                     hasLite: true, latency: 0, measured: true, measuredDb: -26.4, trimDb: 8.4,
                     fileLoudness: true, fileLoudnessDb: -21.5, hasInputLevel: true, inputLevelDbu: 12 };

function slotsSnapshot () {
  return state.layout.type.map((typeKey, slot) => {
    const out = { slot, type: typeKey };
    if (typeKey === 'EMPTY') return out;
    const type = typeByKey.get(typeKey);
    const at = findSlot(slot) || { lane: 0, pos: 0 };
    const role = state.layout.role[slot];
    Object.assign(out, at, {
      colour: typeKey === 'NAM' ? ROLES.find((r) => r.key === role).colour : type.colour,
      onParam: pid(slot, -1),
      params: type.controls.map((_, k) => pid(slot, k)),
      label: typeKey === 'NAM' ? ROLES.find((r) => r.key === role).name : type.name,
    });
    if (typeKey === 'NAM') out.role = role;
    if (type.takesFiles) {
      const f = state.files[slot];
      out.file = f ? { ...f, empty: false, image: imageForPath(f.path), ...bankInfo(f.path),
                       ...(typeKey === 'NAM' ? { model: MODEL_INFO } : {}) }
                   : { bank: '', variant: '', gear: '', empty: true };
    }
    return out;
  });
}

function typesSnapshot () {
  return TYPES.map((t) => ({ ...t, used: countType(t.key) }));
}

function resetRig () {
  for (const d of paramDefs) state.params[d.id] = toUnit(d, d.def);
  state.layout = { type: Array(LIMITS.slots).fill('EMPTY'), role: Array(LIMITS.slots).fill('amp'),
                   lanes: [[], []] };
  state.files = Array.from({ length: LIMITS.slots }, () => null);
  for (const e of DEFAULT_LAYOUT) {
    state.layout.type[e.slot] = e.type;
    state.layout.role[e.slot] = e.role;
    state.layout.lanes[e.lane][e.pos] = e.slot;
    resetSlotControls(e.slot);
  }
  for (const k of Object.keys(state.tools)) state.tools[k] = false;
}

// Stimmgerät auf/zu und MUTE. Dieselben drei Regeln wie in
// plugin/src/PluginProcessor.cpp.
let muteBeforeTuner = false;

function tunerToggled (wasOpen, isOpen) {
  if (wasOpen === isOpen) return;

  if (isOpen) {
    // Öffnen schaltet stumm, nach dem Stand vom letzten Stimmen.
    muteBeforeTuner = state.params.mute >= .5;
    state.params.mute = state.settings.tunerMute ? 1 : 0;
    // Das Programm hat selbst etwas geschaltet, die UI erfährt es nur über die
    // Meldung.
    state.structure++;
    return;
  }

  // Hat man im Stimmgerät den Taster gedrückt, gilt diese Wahl weiter. Sonst
  // stellt Schließen den vorherigen Stand wieder her.
  const muted = state.params.mute >= .5;
  if (muted !== !!state.settings.tunerMute) state.settings.tunerMute = muted;
  else state.params.mute = muteBeforeTuner ? 1 : 0;
  state.structure++;
}

// Dispatch
export async function mockCall (request) {
  const { cmd } = request;
  const ok = (data) => ({ ok: true, data });
  const fail = (error) => ({ ok: false, error });

  switch (cmd) {
    case 'sync':
      return ok({
        paramDefs, params: paramSnapshot(), slots: slotsSnapshot(),
        lanes: state.layout.lanes, types: typesSnapshot(), roles: ROLES,
        tools: TOOLS.map((t) => ({ ...t, shown: state.tools[t.key] })),
        limits: { slots: LIMITS.slots, laneSlots: LIMITS.laneSlots, lanes: LIMITS.lanes },
        zoom: state.zoom, presets: state.presets,
        settings: state.settings, engine: { ...ENGINE, lite: state.settings.engineLite },
        t3k: state.t3k, firstRun: false, structure: state.structure,
      });

    case 'param.set': {
      const base = defById.get(request.id);
      if (!base) return fail('unbekannt');
      const d = effDef(base.id);
      const before = state.params[base.id];
      state.params[base.id] = Math.min(1, Math.max(0, request.unit));
      if (base.id === 'tuner') tunerToggled(before >= .5, state.params.tuner >= .5);
      const value = fromUnit(d, state.params[base.id]);
      return ok({ unit: state.params[d.id], value, text: textOf(d, value) });
    }
    case 'param.begin':
    case 'param.end':
    case 'param.learn': {
      // Ohne MIDI-Controller kommt in der Demo kein Befehl, er wird nach kurzer
      // Zeit erfunden, damit das Anlernen sichtbar ist.
      clearTimeout(state.learnTimer);
      const id = request.id;
      state.learnTimer = setTimeout(() => {
        const used = new Set(state.settings.midiAssignments.map((a) => a.cc));
        let cc = 20;
        while (used.has(cc)) ++cc;
        const d = defById.get(id);
        state.settings.midiAssignments = state.settings.midiAssignments
          .filter((a) => a.param !== id)
          .concat([{ cc, channel: 0, param: id, paramName: d?.name || id }]);
        state.structure++;
        notice(`CC ${cc} → ${d?.name || id}`);
      }, 1500);
      return ok();
    }
    case 'param.learnCancel':
      clearTimeout(state.learnTimer);
      return ok();
    case 'param.reset': {
      if (!defById.get(request.id)) return fail('unbekannt');
      const d = effDef(request.id);
      state.params[request.id] = toUnit(d, d.def);
      return ok({ unit: state.params[request.id], value: d.def, text: textOf(d, d.def) });
    }
    case 'param.forget':
      state.settings.midiAssignments =
        state.settings.midiAssignments.filter((a) => a.param !== request.id);
      state.structure++;
      return ok();

    // Slots
    case 'slot.add': {
      const slot = addSlot(request.type, request.role, request.lane || 0, request.pos ?? 99);
      return slot < 0 ? fail('The slot could not be added.') : ok({ slot });
    }
    case 'slot.remove': removeSlot(request.slot); return ok();
    case 'slot.move': {
      const at = findSlot(request.slot);
      if (!at) return fail('There is no such slot.');
      const keep = state.layout.lanes.map((r) => r.slice());
      removeAt(at.lane, at.pos);
      if (!insertAt(request.lane, request.pos, request.slot)) {
        state.layout.lanes = keep;
        notice('That lane is full', 'warn');
      }
      state.structure++;
      return ok();
    }
    case 'slot.type': {
      const slot = request.slot;
      if (state.layout.type[slot] === request.type) return ok();
      if (request.type === 'EMPTY') { removeSlot(slot); return ok(); }
      const type = typeByKey.get(request.type);
      if (countType(request.type) >= type.limit) {
        notice(`${type.limit} ${type.name} slots are the most the rig can run`, 'warn');
        state.structure++;
        return ok();
      }
      state.layout.type[slot] = request.type;
      state.layout.role[slot] = 'amp';
      state.files[slot] = null;
      resetSlotControls(slot);
      state.structure++;
      return ok();
    }
    case 'slot.role': {
      const slot = request.slot;
      if (state.layout.type[slot] !== 'NAM' || state.layout.role[slot] === request.role) return ok();
      const f = state.files[slot];
      if (f && roleForGear(f.gear) !== request.role) state.files[slot] = null;
      state.layout.role[slot] = request.role;
      if (['pedal', 'outboard', 'space'].includes(request.role)) setSlotValue(slot, 'nam_mix', 1);
      else for (const id of ['amp_bass', 'amp_mid', 'amp_treble']) setSlotValue(slot, id, 0);
      if (request.role === 'amp') ensureIrAfter(slot, true);
      else if (request.role === 'fullrig') { const ir = irAfter(slot); if (ir >= 0) state.params[pid(ir, -1)] = 0; }
      state.structure++;
      return ok();
    }
    case 'slot.place': {
      const slot = slotForFile(request.type, request.role, request.slot ?? -1);
      return slot < 0 ? fail('There is no room for it.') : ok({ slot });
    }
    case 'slot.load': {
      await delay(220);
      // Wie im Programm: das Bild kommt aus der Bibliothek, nicht vom Aufrufer.
      loadIntoSlot(request.slot, { bank: request.bank, variant: request.variant,
                                   gear: request.gear, path: request.path });
      return ok();
    }
    case 'slot.clear':
      state.files[request.slot] = null;
      state.structure++;
      return ok();
    case 'tool.show': {
      state.tools[request.tool] = !!request.shown;
      const param = { LOOPER: 'looper_on', DRUMS: 'drums_on', RECORDER: 'recorder_on' }[request.tool];
      if (param) state.params[param] = request.shown ? 1 : 0;
      if (request.shown && request.tool === 'LOOPER' && !state.tools.DRUMS) {
        state.tools.DRUMS = true;
        state.params.drums_on = 1;
        notice('BEATMACHINE came along with LOOPER');
      }
      state.structure++;
      return ok();
    }
    case 'window.size': state.zoom = request.step; return ok();

    case 'library.list': {
      let typeKey = request.type, role = request.role || 'amp';
      if (request.slot !== undefined) { typeKey = state.layout.type[request.slot]; role = state.layout.role[request.slot]; }
      if (typeKey === 'IR') return ok(LIB.CAB);
      if (typeKey !== 'NAM') return ok([]);
      if (role === 'amp') return ok(LIB.AMP.filter((b) => b.gear !== 'amp-cab'));
      if (role === 'fullrig') return ok(LIB.AMP.filter((b) => b.gear === 'amp-cab'));
      return ok(role === 'pedal' ? LIB.PEDAL : role === 'space' ? LIB.SPACE : LIB.OUTBOARD);
    }
    case 'library.reveal': return ok({ path: state.settings.libraryPath });
    case 'library.import':
      emit('notice', { text: 'There is no file picker in the dry run.', kind: 'warn' });
      return ok();
    case 'library.deleteBank':
    case 'library.deleteVariant': {
      for (const key of Object.keys(LIB)) {
        LIB[key] = LIB[key].filter((b) => b.path !== request.path);
        for (const b of LIB[key]) b.variants = b.variants.filter((v) => v.path !== request.path);
      }
      state.structure++;
      return ok();
    }

    case 'preset.list': return ok(state.presets);
    case 'preset.load':
      state.presets.current = request.name;
      state.structure++;
      return ok();
    case 'preset.save': {
      // Vorschau-Captures kommen spätestens jetzt in die Bibliothek, wie im
      // Programm.
      const kept = state.files.map((f, slot) => (f?.preview ? keepPreview(slot) : null)).filter(Boolean);
      if (!state.presets.list.includes(request.name)) state.presets.list.push(request.name);
      state.presets.current = request.name;
      state.structure++;
      return ok({ kept, notKept: 0 });
    }
    case 'preset.delete':
      state.presets.list = state.presets.list.filter((n) => n !== request.name);
      if (state.presets.current === request.name) state.presets.current = '';
      state.structure++;
      return ok();
    case 'preset.step': {
      const list = state.presets.list;
      if (!list.length) return ok();
      const i = list.indexOf(state.presets.current);
      const n = ((i < 0 ? 0 : i + request.dir) % list.length + list.length) % list.length;
      state.presets.current = list[n];
      state.structure++;
      return ok();
    }
    case 'preset.reset':
      resetRig();
      state.presets.current = '';
      state.structure++;
      return ok();

    // TONE3000
    case 't3k.state': return ok({ ...state.t3k, busy: state.busy });

    case 't3k.connect':
      await delay(900);
      state.t3k.connected = true;
      state.t3k.user = 'player';
      state.structure++;
      return ok({ user: state.t3k.user });

    case 't3k.disconnect':
      state.t3k.connected = false;
      state.t3k.user = '';
      state.structure++;
      return ok();

    case 't3k.search': {
      // Fehlerfall und Wartezeit nachbilden.
      if (!state.t3k.connected) return fail('not-connected');
      await delay(280 + Math.random() * 420);

      if (request.text && request.text.toLowerCase() === 'fehler')
        return fail('TONE3000 answered with 500.');

      const gear = CATEGORY_GEAR[request.category];
      let rows = buildT3k().filter((r) => {
        if (request.tab === 2 && !r.isFavorite) return false;
        if (request.tab === 1 && r.author !== state.t3k.user) return false;
        if (request.category === 3) return r.format === 'ir';
        if (gear) return r.gear === gear;
        return true;
      });

      if (request.text)
        rows = rows.filter((r) => r.title.toLowerCase().includes(request.text.toLowerCase()));

      const pageSize = 25;
      const totalPages = Math.max(1, Math.ceil(rows.length / pageSize));
      const page = Math.min(request.page || 1, totalPages);
      const slice = rows.slice((page - 1) * pageSize, page * pageSize)
                        .map((r) => ({ ...r, here: downloaded.has(r.id) }));

      return ok({ rows: slice, page, totalPages, total: rows.length });
    }

    case 't3k.models': {
      await delay(320);
      const tone = buildT3k().find((r) => r.id === request.tone);
      const count = tone ? tone.models : 1;
      const rows = Array.from({ length: count }, (_, i) => ({
        id: request.tone * 10 + i,
        name: ['Standard', 'Lite', 'Crunch', 'Clean', 'Boosted'][i] || `Model ${i + 1}`,
        size: i === 1 ? 'lite' : 'standard',
        architecture: i === 1 ? '2' : '1',
        here: downloadedModels.has(request.tone * 10 + i),
      }));
      return ok({ rows });
    }

    case 't3k.favourite': {
      const tone = buildT3k().find((r) => r.id === request.tone);
      if (tone) tone.isFavorite = request.on;
      return ok();
    }

    case 't3k.image': {
      // Langsam und nicht immer erfolgreich, um die Warteschlange zu testen.
      await delay(400 + Math.random() * 900);
      const id = Number(String(request.url).split('/').pop());
      if (!Number.isFinite(id)) return fail('Not an image address.');
      if (id % 13 === 0) return fail('Image not reachable.');
      return ok({ uri: mockImage('t3k ' + id) });
    }

    case 't3k.get':
    case 't3k.audition': {
      if (state.busy) return fail('busy');
      state.busy = true;

      const modelId = request.model || request.tone * 10;
      const total = 900 + Math.random() * 900;
      const started = performance.now();

      await new Promise((resolve) => {
        const tick = setInterval(() => {
          const p = Math.min(1, (performance.now() - started) / total);
          emit('t3kProgress', { tone: request.tone, model: modelId, value: p, done: false });
          if (p >= 1) { clearInterval(tick); resolve(); }
        }, 60);
      });

      emit('t3kProgress', { tone: request.tone, model: modelId, done: true });
      state.busy = false;

      if (cmd === 't3k.get') { downloaded.add(request.tone); downloadedModels.add(modelId); }

      const row = buildT3k().find((r) => r.id === request.tone);
      const slot = slotForFile(row?.type || 'NAM', row?.role || 'amp', request.slot ?? -1);
      if (slot >= 0) loadIntoSlot(slot, { bank: request.title, variant: 'Standard', gear: request.gear,
                                          path: `t3k/${request.tone}.nam`, preview: cmd === 't3k.audition',
                                          tone: request.tone, model: modelId });
      return ok({ slot });
    }

    // STORE auf der Karte: die Vorschau bleibt auf ihrem Slot und liegt danach
    // in der Bibliothek, wie im Programm.
    case 't3k.keep': {
      const kept = keepPreview(request.slot);
      if (!kept) return fail('This slot holds no preview.');
      state.structure++;
      return ok(kept);
    }

    case 't3k.page': window.open(request.page, '_blank', 'noopener'); return ok();

    case 't3k.delete':
      downloaded.delete(request.tone);
      state.structure++;
      return ok();

    // Einstellungen
    case 'settings.get': return ok(state.settings);
    case 'settings.set':
      Object.assign(state.settings, request);
      delete state.settings.cmd;
      state.structure++;
      return ok(state.settings);
    case 'settings.midiClear':
      state.settings.midiAssignments = [];
      state.structure++;
      return ok();
    case 'settings.checkUpdate':
      await delay(700);
      return ok({ result: 'none-published', version: state.settings.version });

    case 'eq.enable': state.settings.eqEnabled = request.on; return ok();
    case 'eq.band': {
      const b = state.settings.eqBands[request.band];
      if (!b) return fail('unbekannt');
      for (const key of ['freq', 'gain', 'q', 'shape'])
        if (request[key] !== undefined) b[key] = request[key];
      b.shapeName = ['BELL', 'LOW SHELF', 'HIGH SHELF', 'LOW CUT', 'HIGH CUT'][b.shape];
      state.settings.eqCurveName = 'CUSTOM';
      return ok(state.settings);
    }
    case 'eq.select': {
      const presets = {
        'FLAT': [[80, 0, 1, 0], [200, 0, 1, 0], [500, 0, 1, 0], [1200, 0, 1, 0], [3000, 0, 1, 0], [6000, 0, 1, 0], [10000, 0, 1, 0]],
        'GUITAR CUT': [[85, 0, .72, 3], [180, -1, .9, 0], [420, 0, 1, 0], [1000, 0, 1, 0], [3400, -2, 1.1, 0], [6000, 0, 1, 0], [8500, 0, .72, 4]],
        'SCOOP': [[110, 1.5, .7, 1], [250, 0, 1, 0], [600, -4, .9, 0], [1200, -2, 1.1, 0], [3000, 0, 1, 0], [6000, 0, 1, 0], [7000, 1.5, .7, 2]],
        'LEAD': [[90, 0, 1, 0], [300, -1.5, .9, 0], [700, 0, 1, 0], [1100, 3.5, .8, 0], [2600, 1.5, 1.1, 0], [6000, 0, 1, 0], [12000, 0, .7, 4]],
        'CLEAN UP': [[45, 0, .7, 3], [200, 0, 1, 0], [500, 0, 1, 0], [1200, 0, 1, 0], [3000, 0, 1, 0], [6000, 0, 1, 0], [15000, 0, .7, 4]],
      };
      const rows = presets[request.name];
      if (rows) state.settings.eqBands = rows.map(([freq, gain, q, shape]) =>
        ({ freq, gain, q, shape, shapeName: ['BELL', 'LOW SHELF', 'HIGH SHELF', 'LOW CUT', 'HIGH CUT'][shape] }));
      state.settings.eqCurveName = request.name;
      state.settings.eqEnabled = true;
      emit('notice', { text: `EQ curve: ${request.name}`, kind: 'info' });
      state.structure++;
      return ok(state.settings);
    }
    case 'eq.save':
      if (!state.settings.eqCurves.some((c) => c.name === request.name))
        state.settings.eqCurves.push({ name: request.name, description: null, factory: false });
      state.settings.eqCurveName = request.name;
      emit('notice', { text: `EQ curve saved: ${request.name}`, kind: 'info' });
      state.structure++;
      return ok(state.settings);
    case 'eq.delete':
      state.settings.eqCurves = state.settings.eqCurves.filter((c) => c.name !== request.name || c.factory);
      state.settings.eqCurveName = 'CUSTOM';
      state.structure++;
      return ok(state.settings);
    case 'eq.response':
      // Die Demo rechnet die Kurve selbst mit denselben Formeln wie der Kern.
      return ok({ db: null });

    // Looper
    case 'looper.tap': {
      const tr = state.looper[request.track];
      tr.state = { EMPTY: 'RECORDING', RECORDING: 'PLAYING',
                   PLAYING: 'STOPPED', STOPPED: 'PLAYING' }[tr.state];
      if (tr.state === 'PLAYING' && !tr.rawLength) {
        tr.rawLength = 96000; tr.loopStart = 0; tr.loopEnd = 96000; tr.seconds = 2; tr.beats = 4;
      }
      return ok();
    }
    case 'looper.erase':
      state.looper[request.track] = { state: 'EMPTY', pos: 0, vol: .8, seconds: 0, beats: 0, rawLength: 0, loopStart: 0, loopEnd: 0 };
      return ok();
    case 'looper.eraseAll':
      state.looper = state.looper.map(() => ({ state: 'EMPTY', pos: 0, vol: .8, seconds: 0, beats: 0, rawLength: 0, loopStart: 0, loopEnd: 0 }));
      return ok();
    case 'looper.volume': state.looper[request.track].vol = request.value; return ok();
    case 'looper.edges':
      state.looper[request.track].loopStart = request.start;
      state.looper[request.track].loopEnd = request.end;
      return ok();
    case 'looper.waveform': {
      const tr = state.looper[request.track];
      const n = request.points;
      const peaks = Array.from({ length: n }, (_, i) =>
        tr.rawLength ? Math.abs(Math.sin(i * .21) * Math.exp(-i / (n * .8))) * .8 : 0);
      return ok({ peaks, rawLength: tr.rawLength, loopStart: tr.loopStart,
                  loopEnd: tr.loopEnd, cycle: 96000, sampleRate: 48000 });
    }

    // Recorder
    case 'recorder.tap': {
      // Tippen: aufnehmen, stoppen, abspielen, stoppen. Die Reihenfolge steht
      // in core/src/engine/Recorder.cpp.
      const R = { empty: 0, recording: 1, playing: 2, stopped: 3 };
      const s = state.recorder.state;
      if (s === R.empty) {
        state.recorder.state = R.recording;
        state.recorder.has = true; state.recorder.seconds = 12.5;
      } else if (s === R.recording) state.recorder.state = R.stopped;
      else if (s === R.stopped)     state.recorder.state = R.playing;
      else                          state.recorder.state = R.stopped;
      return ok();
    }
    case 'recorder.erase':
      state.recorder = { state: 0, pos: 0, seconds: 0, has: false, full: false, l: 0, r: 0 };
      return ok();
    case 'recorder.waveform': {
      const n = request.points;
      const peaks = Array.from({ length: n }, (_, i) =>
        state.recorder.has ? Math.abs(Math.sin(i * .17)) * .7 : 0);
      return ok({ peaks, seconds: state.recorder.seconds });
    }
    case 'recorder.save':
      if (!state.recorder.has) return fail('There is no take on the tape.');
      emit('notice', { text: 'Take saved: NAMplify 2026-08-25.wav', kind: 'info' });
      return ok({ path: '~/Documents/NAMplify/Recordings/…' });

    // Audio
    case 'audio.devices':
      return ok({ available: true,
        types: ['CoreAudio'], currentType: 'CoreAudio',
        outputs: ['Built-in Output', 'Scarlett 2i2 USB'],
        inputs: ['Built-in Microphone', 'Scarlett 2i2 USB'],
        currentOutput: 'Scarlett 2i2 USB', currentInput: 'Scarlett 2i2 USB',
        sampleRates: [44100, 48000, 88200, 96000], bufferSizes: [32, 64, 128, 256, 512],
        currentRate: 48000, currentBuffer: 128,
        inputChannels: [0, 1].map((index) => ({
          index, name: `Input ${index + 1}`,
          active: (state.audio.inputChannels ?? [0]).includes(index) })),
        hasControlPanel: false,
        midiInputs: [{ name: 'FCB1010', id: 'a', enabled: true },
                     { name: 'Launch Control', id: 'b', enabled: false }],
        canPairBluetooth: false });
    case 'audio.inputChannels':
      state.audio.inputChannels = request.channels;
      return ok({ available: true });
    case 'audio.levels': {
      // Kanal 1 spielt, Kanal 2 ist still, damit man in der Demo sieht, woran
      // man die Gitarre erkennt.
      const t = performance.now() / 1000;
      const guitar = -18 + 12 * Math.abs(Math.sin(t * 2.1)) * Math.exp(-((t * 1.3) % 1) * 3);
      return ok({ levels: [guitar, -72] });
    }
    case 'audio.meter':
    case 'audio.controlPanel':
    case 'audio.set':
    case 'audio.type':
    case 'audio.midiInput':
    case 'audio.pairBluetooth':
    case 'audio.systemDialog':
      return ok({ available: true });

    case 'firstrun.done':
      return ok();
    case 'engine.lite':
      state.settings.engineLite = request.lite;
      ENGINE.lite = request.lite;
      return ok();
    case 'engine.oversampling':
      state.settings.captureOversampling = request.factor;
      return ok(state.settings);
    case 'visible': return ok();
    case 'tuner.watch': return ok();

    default: return fail(`Unbekannter Befehl: ${cmd}`);
  }
}

const downloaded = new Set();
const downloadedModels = new Set();

// Anfangszustand: Werksaufbau, ein Amp und eine Box geladen.
resetRig();
{
  const amp = LIB.AMP[0], cab = LIB.CAB[0];
  state.files[0] = { bank: amp.title, variant: amp.variants[0].name, gear: 'amp', path: amp.variants[0].path };
  state.files[1] = { bank: cab.title, variant: cab.variants[0].name, gear: 'cab', path: cab.variants[0].path };
}
