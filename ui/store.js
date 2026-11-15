// Zustand der Seite. Eine Kopie des Zustands im Programm: resync() holt alles
// neu, und alles Sichtbare wird daraus gezeichnet.

import { ask, call, on } from './bridge.js';

const listeners = new Set();

export const store = {
  paramDefs: [],
  defById: new Map(),
  params: {},
  slots: [],          // 16 Slots, leere mit type 'EMPTY'
  lanes: [[], []],    // je Spur die Slotnummern in Kettenreihenfolge
  types: [],
  typeByKey: new Map(),
  roles: [],
  roleByKey: new Map(),
  tools: [],
  limits: { slots: 16, laneSlots: 12, lanes: 2 },
  zoom: 2,
  presets: { list: [], current: '' },
  settings: null,
  engine: null,
  t3k: { hasKey: false, connected: false, user: '' },
  firstRun: false,
  meters: null,
  ready: false,
};

export function subscribe (fn) { listeners.add(fn); return () => listeners.delete(fn); }

export function notify (what = 'all') {
  for (const fn of listeners) { try { fn(what); } catch (e) { console.error(e); } }
}

export async function resync () {
  const data = await ask('sync');

  store.paramDefs = data.paramDefs;
  store.defById = new Map(data.paramDefs.map((d) => [d.id, d]));
  store.params = data.params;
  store.slots = data.slots;
  store.lanes = data.lanes;
  store.types = data.types;
  store.typeByKey = new Map(data.types.map((t) => [t.key, t]));
  store.roles = data.roles;
  store.roleByKey = new Map(data.roles.map((r) => [r.key, r]));
  store.tools = data.tools;
  store.limits = data.limits;
  store.zoom = data.zoom;
  store.presets = data.presets;
  store.settings = data.settings;
  store.engine = data.engine;
  store.t3k = data.t3k;
  store.firstRun = data.firstRun;
  store.ready = true;

  notify('all');
  return data;
}

// Regler

/// Beschreibung eines Reglers. Ein Slot-Regler (s3_k2) läuft im Host von 0 bis
/// 1. Name, Bereich, Einheit und Werkswert hängen vom Typ ab, der gerade auf
/// dem Slot liegt. Beides kommt vom Programm.
export function def (id) {
  const d = store.defById.get(id);
  if (!d || d.slot === undefined || d.k < 0) return d;
  const type = store.typeByKey.get(store.slots[d.slot]?.type);
  const c = type?.controls?.[d.k];
  return c ? { ...c, id, index: d.index, slot: d.slot, k: d.k } : d;
}
export function param (id) { return store.params[id] || { unit: 0, value: 0, text: '' }; }
export function paramValue (id) { return param(id).value; }
export function paramUnit (id) { return param(id).unit; }
export function paramOn (id) { return param(id).value >= 0.5; }

/// Setzt einen Regler und übernimmt den Wert, den das Programm zurückgibt. Der
/// Host darf begrenzen, die Anzeige zeigt dann den begrenzten Wert.
export async function setParamUnit (id, unit, { silent = false } = {}) {
  const reply = await call('param.set', { id, unit });
  if (reply?.ok && reply.data) {
    store.params[id] = reply.data;
    if (!silent) notify('param:' + id);
  }
  return store.params[id];
}

export async function setParamValue (id, value) {
  const d = def(id);
  if (!d) return;
  return setParamUnit(id, valueToUnit(d, value));
}

export async function toggleParam (id) {
  return setParamUnit(id, paramOn(id) ? 0 : 1);
}

export async function resetParam (id) {
  const reply = await call('param.reset', { id });
  if (reply?.ok) {
    const d = def(id);
    store.params[id] = { unit: reply.data.unit, value: reply.data.value,
                         text: valueText(d, reply.data.value) };
    notify('param:' + id);
  }
}

export function beginGesture (id) { call('param.begin', { id }); }
export function endGesture (id) { call('param.end', { id }); }

// Abbildungen 0…1

export function valueToUnit (d, v) {
  if (!d) return 0;
  if (d.kind === 'bool') return v >= .5 ? 1 : 0;
  if (d.kind === 'choice') return d.max > 0 ? Math.min(1, Math.max(0, v / d.max)) : 0;
  if (d.kind === 'log') {
    const lo = Math.max(1e-9, d.min);
    return Math.log(Math.min(d.max, Math.max(lo, v)) / lo) / Math.log(d.max / lo);
  }
  return (Math.min(d.max, Math.max(d.min, v)) - d.min) / (d.max - d.min);
}

export function unitToValue (d, u) {
  if (!d) return 0;
  u = Math.min(1, Math.max(0, u));
  if (d.kind === 'bool') return u >= .5 ? 1 : 0;
  if (d.kind === 'choice') return Math.round(u * d.max);
  if (d.kind === 'log') { const lo = Math.max(1e-9, d.min); return lo * Math.pow(d.max / lo, u); }
  return d.min + (d.max - d.min) * u;
}

export function valueText (d, v) {
  if (!d) return '';
  if (d.kind === 'bool') return v >= .5 ? 'ON' : 'OFF';
  if (d.kind === 'choice') return d.choices?.[Math.round(v)] ?? '';
  const span = d.max - d.min;
  const s = span >= 100 ? v.toFixed(0) : span >= 5 ? v.toFixed(1) : v.toFixed(2);
  return d.unit ? `${s} ${d.unit}` : s;
}

/// Werkswert eines Reglers, für den Doppelklick.
export function defaultUnit (id) {
  const d = def(id);
  return d ? valueToUnit(d, d.def) : 0;
}

// Slots

export function slot (n) { return store.slots[n]; }
export function slotType (n) { return store.typeByKey.get(store.slots[n]?.type); }
export function slotColour (n) { return store.slots[n]?.colour || '#8a8a8a'; }
export function slotEnabled (n) { const s = store.slots[n]; return !!s?.onParam && paramOn(s.onParam); }
export async function setSlotEnabled (n, on) {
  const s = store.slots[n];
  if (s?.onParam) await setParamUnit(s.onParam, on ? 1 : 0);
}

/// Parameterkennung eines Reglers auf einem Slot, gefunden über die Kennung im
/// Typ (drive_gain, nam_level …).
export function slotParam (n, controlId) {
  const s = store.slots[n];
  const type = slotType(n);
  const k = type ? type.controls.findIndex((c) => c.id === controlId) : -1;
  return k >= 0 ? s.params[k] : null;
}

/// Zeigt die Karte dieses NAM-Slots den Regler? Die Antwort kommt vom Programm
/// (roles am Regler, aus namControlShown).
export function controlShown (n, controlId) {
  const s = store.slots[n];
  const c = slotType(n)?.controls.find((x) => x.id === controlId);
  if (!c) return false;
  return !c.roles || c.roles.includes(s.role);
}

/// Alle belegten Slots in Kettenreihenfolge, Spur A, dann B.
export function chainOrder (withB = true) {
  return withB ? [...store.lanes[0], ...store.lanes[1]] : [...store.lanes[0]];
}

export function tool (key) { return store.tools.find((t) => t.key === key); }

// Meter

on('meters', (m) => { store.meters = m; notify('meters'); });
