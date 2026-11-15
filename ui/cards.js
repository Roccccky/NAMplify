// Inhalt der Karte eines Slots.
//
// Die Karte zeigt den in der Kette gewählten Slot. Welche Regler es gibt, ihr
// Bereich, ihr Werkswert und welche eine Rolle zeigt, kommt vom Programm
// (types[].controls). Hier steht nur die Anordnung in benannten Gruppen.

import {
  makeKnob, makeFaderBank, makeToggle, makeStateToggle, makeSelector,
  makeBeatLights, el,
} from './controls.js';
import {
  store, paramValue, setParamValue, notify, slot as slotInfo, slotParam, controlShown,
} from './store.js';
import { buildLooperCard } from './looper.js';
import { buildRecorderCard } from './recorder.js';

/// Die Werkswerte eines Geräts kommen vom Programm.
function driveDefaults (index) { return store.engine?.driveDefaults?.[index]; }
function reverbDefaults (index) { return store.engine?.reverbDefaults?.[index]; }

function group (parent, name, nodes) {
  const live = nodes.filter(Boolean);
  if (!live.length) return null;
  const g = el('div', 'group', parent);
  el('div', 'group-name', g).textContent = name;
  const body = el('div', 'group-body', g);
  for (const n of live) body.appendChild(n);
  return g;
}

/// Baut die Regler eines Slots. card.parts sammelt alles, was beim Auffrischen
/// neu gezeichnet wird.
export function buildSlotControls (n, card, colour) {
  const s = slotInfo(n);
  const root = el('div', 'slot-controls');
  const parts = [];
  const selectors = el('div', 'slot-selectors', root);
  const groups = el('div', 'groups', root);

  const P = (id) => slotParam(n, id);
  const reg = (node) => { if (node) parts.push(node); return node; };
  const K = (id, label, o = {}) => {
    const pid = P(id);
    return pid ? reg(makeKnob(pid, { colour, label, ...o })) : null;
  };

  switch (s.type) {
    case 'GATE':
      // Am linken Anschlag ist der Block aus.
      group(groups, 'GATE', [K('gate_threshold', 'THRESH', { offAtMin: true })]);
      break;

    case 'COMP':
      group(groups, 'COMP', [K('comp_sustain', 'SUSTAIN')]);
      group(groups, 'OUTPUT', [K('comp_level', 'LEVEL')]);
      break;

    case 'DRIVE': {
      const model = P('drive_model');
      selectors.appendChild(reg(makeSelector(model, {
        colour,
        onChange: (i) => {
          // Beim Modellwechsel springen die vier Regler auf die Werte des neuen
          // Modells, nur hier, nicht beim Aufbau.
          const d = driveDefaults(i);
          if (!d) return;
          setParamValue(P('drive_gain'), d.gain);
          setParamValue(P('drive_level'), d.level);
          setParamValue(P('drive_tone'), d.tone);
          setParamValue(P('drive_body'), d.body).then(() => notify('drive'));
        },
      })));
      group(groups, 'DRIVE', [K('drive_gain', 'GAIN'), K('drive_tone', 'TONE'), K('drive_body', 'BODY')]);
      group(groups, 'OUTPUT', [K('drive_level', 'LEVEL')]);
      break;
    }

    case 'NAM': {
      // Was die Rolle zeigt, sagt das Programm (controlShown). Ein nicht
      // gezeigter Regler wirkt auch nicht.
      const show = (id) => controlShown(n, id);
      group(groups, 'INPUT', [show('nam_gain') && K('nam_gain', 'GAIN')]);
      if (show('amp_bass'))
        group(groups, 'TONE', [K('amp_bass', 'BASS'), K('amp_mid', 'MID'), K('amp_treble', 'TREBLE')]);
      group(groups, 'OUTPUT', [
        K('nam_level', 'LEVEL'),
        show('nam_mix') && K('nam_mix', 'MIX'),
        // RAW, CALIBRATED, NORMALIZED als Schalter mit drei Stufen, kurz
        // beschriftet, damit er in die Reglerzeile passt.
        show('amp_norm') && reg(makeSelector(P('amp_norm'), { colour, terms: ['RAW', 'CAL', 'NORM'] })),
      ]);
      break;
    }

    case 'IR':
      group(groups, 'OUTPUT', [K('cab_output', 'OUTPUT')]);
      break;

    case 'EQ': {
      // Beide EQ-Varianten bleiben gleichzeitig vorhanden und werden nur
      // umgeblendet.
      const three = group(groups, '3 BAND', [K('eq3_bass', 'BASS'), K('eq3_mid', 'MID'), K('eq3_treble', 'TREBLE')]);
      const ids = ['eq7_100', 'eq7_200', 'eq7_400', 'eq7_800', 'eq7_1600', 'eq7_3200', 'eq7_6400'];
      const bank = reg(makeFaderBank(ids.map(P), ['100', '200', '400', '800', '1k6', '3k2', '6k4'], { colour }));
      const seven = group(groups, '7 BAND', [bank]);
      if (seven) seven.style.height = '210px';

      const mode = P('eq_mode');
      const applyMode = () => {
        const sevenOn = paramValue(mode) >= .5;
        if (three) three.style.display = sevenOn ? 'none' : 'flex';
        if (seven) seven.style.display = sevenOn ? 'flex' : 'none';
      };
      selectors.appendChild(reg(makeSelector(mode, { colour, terms: ['3 BAND', '7 BAND'], onChange: applyMode })));
      card.apply = applyMode;
      applyMode();
      break;
    }

    case 'CHORUS':
      group(groups, 'MOD', [K('chorus_rate', 'RATE'), K('chorus_depth', 'DEPTH')]);
      group(groups, 'OUTPUT', [K('chorus_mix', 'MIX')]);
      break;

    case 'FLANGER':
      group(groups, 'MOD', [K('flanger_rate', 'RATE'), K('flanger_depth', 'DEPTH'),
                            K('flanger_feedback', 'FDBK'), K('flanger_manual', 'MANUAL')]);
      group(groups, 'OUTPUT', [K('flanger_mix', 'MIX')]);
      break;

    case 'PHASER':
      group(groups, 'MOD', [K('phaser_rate', 'RATE'), K('phaser_depth', 'DEPTH')]);
      group(groups, 'OUTPUT', [K('phaser_mix', 'MIX')]);
      break;

    case 'TREMOLO': {
      const rate = K('tremolo_rate', 'RATE');
      const div = K('sync_tremolo_div', 'DIV');
      group(groups, 'MOD', [rate, div, K('tremolo_depth', 'DEPTH')]);
      const sync = P('sync_tremolo');
      const applySync = () => {
        const on = paramValue(sync) >= .5;
        if (rate) rate.style.display = on ? 'none' : 'flex';
        if (div) div.style.display = on ? 'flex' : 'none';
      };
      const t = reg(makeToggle(sync, 'FREE', 'SYNC', { colour }));
      t.addEventListener('click', () => setTimeout(applySync, 0));
      selectors.appendChild(t);
      card.apply = applySync;
      applySync();
      break;
    }

    case 'DELAY': {
      selectors.appendChild(reg(makeSelector(P('delay_type'), { colour })));
      const time = K('delay_time', 'TIME');
      const div = K('sync_delay_div', 'DIV');
      group(groups, 'DELAY', [time, div, K('delay_feedback', 'FDBK')]);
      group(groups, 'OUTPUT', [K('delay_mix', 'MIX')]);
      const sync = P('sync_delay');
      const applySync = () => {
        const on = paramValue(sync) >= .5;
        if (time) time.style.display = on ? 'none' : 'flex';
        if (div) div.style.display = on ? 'flex' : 'none';
      };
      const t = reg(makeToggle(sync, 'FREE', 'SYNC', { colour }));
      t.addEventListener('click', () => setTimeout(applySync, 0));
      selectors.appendChild(t);
      card.apply = applySync;
      applySync();
      break;
    }

    case 'REVERB': {
      // Der vierte Regler wechselt: SPRING hat keine Raumgröße, dafür die
      // Stärke des Zirps.
      const size = K('reverb_size', 'SIZE');
      const boing = K('reverb_boing', 'BOING');
      group(groups, 'SPACE', [K('reverb_time', 'TIME'), size, boing]);
      group(groups, 'OUTPUT', [K('reverb_tone', 'TONE'), K('reverb_mix', 'MIX')]);
      const type = P('reverb_type');
      const applyType = () => {
        const spring = Math.round(paramValue(type)) === 3;
        if (size) size.style.display = spring ? 'none' : 'flex';
        if (boing) boing.style.display = spring ? 'flex' : 'none';
      };
      selectors.appendChild(reg(makeSelector(type, { colour,
        onChange: (i) => {
          applyType();
          const d = reverbDefaults(i);
          if (!d) return;
          setParamValue(P('reverb_time'), d.time);
          setParamValue(P('reverb_size'), d.size);
          setParamValue(P('reverb_boing'), d.boing);
          setParamValue(P('reverb_tone'), d.tone);
          setParamValue(P('reverb_mix'), d.mix).then(() => notify('reverb'));
        } })));
      card.apply = applyType;
      applyType();
      break;
    }

    default:
      // Ein Typ ohne eigene Anordnung zeigt seine Regler der Reihe nach.
      group(groups, s.label, (store.typeByKey.get(s.type)?.controls || [])
        .map((c) => K(c.id, c.name.toUpperCase())));
      break;
  }

  if (!selectors.childElementCount) selectors.remove();
  card.parts = parts;
  return root;
}

/// Die Werkzeuge am Ende der Kette: Looper, Beat Machine, Recorder.
export function buildToolBody (key, card, colour) {
  const body = el('div', 'tool-body');
  // Die Werkzeuge zeichnen ihren Zustand in einen Streifen. Er ist hier
  // unsichtbar, wird aber erwartet.
  const stripe = el('div', 'card-stripe', body);
  card.stripeFill = el('div', 'card-stripe-fill', stripe);
  const parts = [];

  if (key === 'LOOPER') {
    // Die Taster stehen vorn in jeder Spur (looper.js).
    buildLooperCard(body, card, colour);
  } else if (key === 'RECORDER') {
    buildRecorderCard(body, card, colour);
  } else if (key === 'DRUMS') {
    // Im Klick-Modus ersetzt ein zweiter Pattern-Regler mit vier Stellungen den
    // ersten.
    const K = (id, label) => { const k = makeKnob(id, { colour, label }); parts.push(k); return k; };
    const beatPattern = K('drums_pattern', 'PATTERN');
    const clickPattern = K('drums_click', 'PATTERN');
    const groups = el('div', 'groups', body);
    groups.style.padding = '0';
    group(groups, 'BEAT', [beatPattern, clickPattern, K('drums_bpm', 'BPM')]);
    group(groups, 'OUTPUT', [K('drums_volume', 'VOL')]);

    // DRUMS hat statt eines Displays 16 Taktleuchten.
    card.beats = makeBeatLights(colour);
    const lights = el('div', '', body);
    lights.style.padding = '14px 0 4px';
    lights.appendChild(card.beats);

    const mode = makeToggle('drums_mode', 'BEAT', 'CLICK', { colour });
    parts.push(mode);
    const modeRow = el('div', 'tool-switches', body);
    modeRow.appendChild(mode);

    const applyMode = () => {
      const click = paramValue('drums_mode') >= .5;
      beatPattern.style.display = click ? 'none' : 'flex';
      clickPattern.style.display = click ? 'flex' : 'none';
    };
    mode.addEventListener('click', () => setTimeout(applyMode, 0));
    card.apply = applyMode;
    applyMode();
  }

  card.parts = parts;
  return body;
}
