// Namensvorschlag für das Brett aus dem, was darauf liegt: Amp oder Capture,
// wie viel Gain, und was auffällt (Pedal davor, Delay, Hall, Modulation). Ohne
// Netz und ohne Zufall: dasselbe Brett ergibt dieselben Vorschläge, der
// Zauberstab im Speichern-Dialog blättert durch Varianten.

import { store, slotEnabled, slotParam } from './store.js';

const MAX = 32;
const CORE_MAX = 20;
const GAINS = ['Clean', 'Crunch', 'Lead'];

// Was in Titeln von Captures steht, aber nichts über den Klang sagt.
const NOISE = /\b(?:nam|capture|captures|profile|model|tone3000|neural amp modeler|standard|lite|feather|nano|a1|a2|v\d+)\b/gi;
const MOD = new Set(['CHORUS', 'FLANGER', 'PHASER', 'TREMOLO']);
const DRIVE_NAMES = { SCREAM: 'Scream', WARM: 'Warm', CLEAN: 'Boost', CRUSH: 'Crush' };

/// Nur Buchstaben, Ziffern, Leerzeichen, Punkt und Strich, damit der Name als
/// Dateiname taugt.
function safe (raw) {
  return String(raw || '').normalize('NFKD').replace(/[̀-ͯ]/g, '')
    .replace(/[^A-Za-z0-9 .-]+/g, ' ').replace(/\s+/g, ' ').trim()
    .replace(/^[\s.-]+|[\s.-]+$/g, '');
}

/// An einer Wortgrenze kürzen.
function cut (text, max) {
  if (text.length <= max) return text;
  let out = '';
  for (const word of text.split(' ')) {
    const next = out ? `${out} ${word}` : word;
    if (next.length > max) break;
    out = next;
  }
  return (out || text.slice(0, max)).replace(/[\s.-]+$/g, '');
}

function gainFromText (text) {
  if (/clean|glass|sparkle|jazz/i.test(text)) return 'Clean';
  if (/crunch|edge|break.?up|blues|rhythm/i.test(text)) return 'Crunch';
  if (/lead|high.?gain|hi.?gain|metal|djent|solo/i.test(text)) return 'Lead';
  return null;
}

/// Die aktiven Slots in Kettenreihenfolge.
function activeSlots () {
  const out = [];
  for (const lane of store.lanes || [])
    for (const n of lane) {
      const s = store.slots[n];
      if (s && s.type !== 'EMPTY' && slotEnabled(n)) out.push({ ...s, n });
    }
  return out;
}

const loaded = (s) => s.type === 'NAM' && s.file && !s.file.empty && s.file.bank;

/// Die Teile, aus denen die Varianten gebaut werden.
export function presetNameParts () {
  const active = activeSlots();
  const amp = active.find((s) => loaded(s) && (s.role === 'amp' || s.role === 'fullrig'))
           ?? active.find((s) => loaded(s) && s.role !== 'pedal');
  const coreText = amp ? amp.file.bank : '';
  const core = cut(safe(coreText.replace(NOISE, ' ')), CORE_MAX);

  // Ein Pedal davor: ein NAM-Pedal mit seinem Namen, oder der eingebaute
  // DRIVE mit seinem Modell.
  const pedals = active.filter((s) => (loaded(s) && s.role === 'pedal') || s.type === 'DRIVE');
  const pedalName = (s) => {
    if (s.type !== 'DRIVE') return cut(safe(s.file.bank.replace(NOISE, ' ')), 12);
    const id = slotParam(s.n, 'drive_model');
    const choice = id ? store.params[id]?.text : '';
    return DRIVE_NAMES[String(choice || '').toUpperCase()] || 'Drive';
  };

  let gain = amp ? gainFromText(`${amp.file.bank} ${amp.file.variant || ''}`) : null;
  if (pedals.length) gain = gain ? GAINS[Math.min(2, GAINS.indexOf(gain) + 1)] : 'Crunch';
  if (gain && new RegExp(`\\b${gain}\\b`, 'i').test(core)) gain = null;

  const effects = [];
  if (pedals.length) effects.push(pedalName(pedals[0]));
  if (active.some((s) => s.type === 'DELAY')) effects.push('Delay');
  if (active.some((s) => s.type === 'REVERB' || (loaded(s) && s.role === 'space'))) effects.push('Verb');
  if (active.some((s) => MOD.has(s.type))) effects.push('Mod');
  return { core, gain, effects };
}

/// Ein Vorschlag. variant blättert durch Varianten. Nie ein Name, den schon ein
/// anderes Preset trägt.
export function suggestPresetName (variant = 0) {
  const { core, gain, effects } = presetNameParts();
  const head = [core, gain].filter(Boolean).join(' ');
  const ambience = effects.filter((e) => ['Delay', 'Verb', 'Mod'].includes(e));
  const all = [
    [head, effects.join(' ')],
    [head, ambience.join(' ')],
    [head],
    [[gain, effects[0]].filter(Boolean).join(' '), core],
  ].map((parts) => parts.filter(Boolean).join(' - '))
   .map((text) => cut(safe(text), MAX))
   .filter(Boolean);
  const unique = [...new Set(all)];
  const base = unique.length ? unique[((variant % unique.length) + unique.length) % unique.length] : 'New Preset';

  const own = (store.presets?.current || '').trim().toLowerCase();
  const taken = new Set((store.presets?.list || []).map((n) => n.trim().toLowerCase()).filter((n) => n !== own));
  if (!taken.has(base.toLowerCase())) return base;
  for (let number = 2; ; number += 1) {
    const candidate = `${cut(base, MAX - String(number).length - 1)} ${number}`;
    if (!taken.has(candidate.toLowerCase())) return candidate;
  }
}
