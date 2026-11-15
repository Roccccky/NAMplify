// Einstellungen: links die Seiten mit Symbol, rechts eine Seite. Beim Öffnen
// wird alles neu abgefragt (Geräte, MIDI-Ports, Konto, Pegel), weil Änderungen
// von außen sich nicht melden.

import { el, makeToggle, makeSelector, pegelSkala } from './controls.js';
import { icon } from './icons.js';
import { store, resync, paramValue, setParamUnit, def, valueToUnit } from './store.js';
import { call, ask, tryAsk } from './bridge.js';
import { showDialog, showToast, showMenu } from './dialogs.js';
import { buildEqEditor } from './eqcurve.js';

// Reihenfolge: AUDIO SETTINGS, TONE3000, GENERAL SETTINGS, GLOBAL EQ, MIDI,
// LIBRARY. Meist will man zuerst ans Audiogerät.
const PAGES = [
  { id: 'audio',   name: 'AUDIO SETTINGS',   icon: 'speaker' },
  { id: 't3k',     name: 'TONE3000',         icon: 'cloud' },
  { id: 'general', name: 'GENERAL SETTINGS', icon: 'sliders' },
  { id: 'eq',      name: 'GLOBAL EQ',        icon: 'eq' },
  { id: 'midi',    name: 'MIDI',             icon: 'midi' },
  { id: 'library', name: 'LIBRARY',          icon: 'books' },
];

let currentPage = 'audio';
let devices = null;
let open = false;

export function isSettingsOpen () { return open; }

export async function openSettings (pageId = null) {
  open = true;
  if (pageId && PAGES.some((p) => p.id === pageId)) currentPage = pageId;
  // Neu abfragen
  await resync();
  const [d] = await tryAsk('audio.devices');
  devices = d;
  render();
}

export function closeSettings () {
  open = false;
  stopInputMeters();
  const overlay = document.getElementById('overlay-settings');
  overlay.hidden = true;
  overlay.innerHTML = '';
}

function row (parent, name, hint, { disabled = false } = {}) {
  const node = el('div', 'setting-row', parent);
  if (disabled) node.dataset.disabled = 'true';
  const label = el('div', 'setting-label', node);
  el('div', 'setting-name', label).textContent = name;
  // Hinweistext in der Zeile, zu der er gehört.
  if (hint) el('div', 'setting-hint', label).textContent = hint;
  node.control = el('div', 'setting-control', node);
  return node;
}

function pills (parent, options, current, onPick) {
  const group = el('div', 'pill-group', parent);
  for (const [value, label] of options) {
    const b = el('button', 'pill', group);
    b.textContent = label;
    b.dataset.on = value === current ? 'true' : 'false';
    b.addEventListener('click', () => onPick(value));
  }
  return group;
}

function dropdown (parent, options, current, onPick) {
  const sel = el('select', '', parent);

  // Steht der aktuelle Wert nicht in der Liste, wird er trotzdem gezeigt. Sonst
  // zeigt ein select seinen ersten Eintrag und damit ein Gerät, das gar nicht
  // eingestellt ist.
  const gefunden = options.some((v) => String(v) === String(current));
  if (!gefunden && current !== undefined && current !== null && String(current) !== '') {
    const o = document.createElement('option');
    o.value = String(current);
    o.textContent = String(current);
    o.selected = true;
    sel.appendChild(o);
  }

  for (const value of options) {
    const o = document.createElement('option');
    o.value = String(value);
    o.textContent = String(value);
    if (String(value) === String(current)) o.selected = true;
    sel.appendChild(o);
  }
  sel.addEventListener('change', () => onPick(sel.value));
  return sel;
}

function button (parent, label, onClick, opts = {}) {
  const b = el('button', 'text-button', parent);
  b.textContent = label;
  if (opts.danger) b.dataset.danger = 'true';
  b.addEventListener('click', onClick);
  return b;
}

// Zeichnen
function render () {
  const overlay = document.getElementById('overlay-settings');
  overlay.innerHTML = '';
  overlay.hidden = false;

  const scrim = el('div', 'overlay-scrim', overlay);
  scrim.addEventListener('click', closeSettings);

  const sheet = el('div', 'settings-sheet', overlay);

  const nav = el('div', 'settings-nav', sheet);
  for (const page of PAGES) {
    const b = el('button', 'settings-nav-item', nav);
    b.appendChild(icon(page.icon));
    const label = el('span', '', b);
    label.textContent = page.name;
    b.dataset.on = currentPage === page.id ? 'true' : 'false';
    b.addEventListener('click', () => { currentPage = page.id; render(); });
  }

  stopInputMeters();
  const page = el('div', 'settings-page', sheet);
  ({ audio: pageAudio, t3k: pageT3k, general: pageGeneral,
     eq: pageEq, midi: pageMidi, library: pageLibrary })[currentPage](page);

  const onKey = (e) => {
    if (e.key === 'Escape') { e.stopPropagation(); closeSettings(); window.removeEventListener('keydown', onKey, true); }
  };
  window.addEventListener('keydown', onKey, true);
}

// AUDIO SETTINGS
function pageAudio (page) {
  el('div', 'settings-title', page).textContent = 'AUDIO SETTINGS';

  if (!store.engine?.standalone || !devices?.available) {
    el('div', 'settings-intro', page).textContent =
      'The host decides device, sample rate and buffer.';
  } else {
    el('div', 'settings-section', page).textContent = 'DEVICE';

    // Ausgang vor Eingang, wie im Systemdialog. Jede Zeile erscheint nur, wenn
    // es etwas auszuwählen gibt.
    if (devices.outputs?.length) {
      const r = row(page, 'Output device', null);
      dropdown(r.control, devices.outputs, devices.currentOutput,
        (v) => call('audio.set', { output: v }).then(refreshDevices));
    }
    if (devices.inputs?.length) {
      const r = row(page, 'Input device', null);
      dropdown(r.control, devices.inputs, devices.currentInput,
        (v) => call('audio.set', { input: v }).then(refreshDevices));
    }
    if (devices.inputChannels?.length) inputChannelRow(page);
    if (devices.sampleRates?.length) {
      const r = row(page, 'Sample rate', null);
      dropdown(r.control, devices.sampleRates, devices.currentRate,
        (v) => call('audio.set', { sampleRate: Number(v) }).then(refreshDevices));
    }
    if (devices.bufferSizes?.length) {
      const r = row(page, 'Buffer size', null);
      dropdown(r.control, devices.bufferSizes, devices.currentBuffer,
        (v) => call('audio.set', { bufferSize: Number(v) }).then(refreshDevices));
    }

    if (devices.hasControlPanel) {
      const r = row(page, 'Driver control panel',
        'What the driver itself offers. For ASIO often the only place to set the buffer.');
      button(r.control, 'OPEN', () => call('audio.controlPanel').then(refreshDevices));
    }

    const sys = row(page, 'System dialog',
      'Only there can you pick the driver type. On Windows the difference '
      + 'between usable and unusable.');
    button(sys.control, 'OPEN', () => call('audio.systemDialog'));
  }

  // CAPTURES. Oversampling: ein Capture erzeugt Obertöne über der halben
  // Samplerate, die unharmonisch zurückfalten. Mit doppelter oder vierfacher
  // Rate haben sie Platz. Die Rechenzeit steigt um den Faktor, deshalb steht er
  // in der Zeile.
  el('div', 'settings-section', page).textContent = 'CAPTURES';
  const os = row(page, 'Oversampling',
    'Less aliasing in high-gain captures. Costs 2× or 4× the capture CPU. '
    + 'LSTM captures cannot be split and stay at 1×.');
  pills(os.control, [[1, '1×'], [2, '2×'], [4, '4×']], store.settings?.captureOversampling ?? 1,
    async (v) => { await call('engine.oversampling', { factor: v }); await resync(); render(); });

  // INPUT CALIBRATION
  el('div', 'settings-section', page).textContent = 'INPUT CALIBRATION';

  const models = store.slots
    .filter((s) => s.type === 'NAM' && s.file?.model)
    .map((s) => ({ id: s.label, info: s.file.model }));

  const anyKnowsLevel = models.some((m) => m.info.hasInputLevel);

  const cal = row(page, 'Interface reference level',
    'The analogue level in dBu that equals 0 dBFS. It is in the data sheet of '
    + 'your interface, so this is a number, not a list of fixed steps.',
    { disabled: !anyKnowsLevel });

  const dbuField = el('div', 'eq-field-value', cal.control);
  dbuField.style.minWidth = '90px';
  dbuField.textContent = `${(store.settings?.interfaceDbu ?? 12).toFixed(1)} dBu`;
  dbuField.addEventListener('click', async () => {
    const answer = await showDialog({
      title: 'INTERFACE REFERENCE LEVEL',
      text: 'In dBu. What 0 dBFS equals at your interface output.',
      input: String(store.settings?.interfaceDbu ?? 12), confirm: 'SET',
    });
    if (answer === null) return;
    const n = parseFloat(answer.replace(',', '.'));
    if (!isFinite(n)) return;
    await call('settings.set', { interfaceDbu: n });
    await resync();
    render();
  });

  if (!anyKnowsLevel) {
    el('div', 'setting-hint', cal.querySelector('.setting-label')).textContent =
      'Greyed out while no loaded capture knows its recording level.';
  }

  // Darunter im Klartext, was beim Laden gemessen und angewendet wurde.
  el('div', 'settings-section', page).textContent = 'WHAT WAS MEASURED';

  if (!models.length) {
    const r = row(page, 'No capture loaded', 'Once one is in place, what was measured shows up here.');
  } else {
    for (const { id, info } of models) {
      const parts = [];
      if (info.measured) {
        parts.push(`measured ${info.measuredDb.toFixed(1)} dB`);
        parts.push(`applied ${info.trimDb >= 0 ? '+' : ''}${info.trimDb.toFixed(1)} dB`);
      } else {
        parts.push('not measurable');
      }
      if (info.fileLoudness)
        parts.push(`file says ${info.fileLoudnessDb.toFixed(1)} dB`);
      if (info.hasInputLevel)
        parts.push(`input level ${info.inputLevelDbu.toFixed(1)} dBu`);
      parts.push(info.resampling
        ? `${(info.modelRate / 1000).toFixed(1)} kHz → ${(store.engine.sampleRate / 1000).toFixed(1)} kHz`
        : `${(info.modelRate / 1000).toFixed(1)} kHz, no resampling`);
      if (info.hasLite) parts.push('carries a second net size');
      if (info.oversampling > 1) parts.push(`${info.oversampling}× oversampled`);

      row(page, id, parts.join('   ·   '));
    }
  }
}

// Eingangskanäle: ein Kanal heißt Mono und geht auf beide Seiten der Kette,
// zwei nebeneinander heißen Stereo. Ohne diese Wahl nähme die Standalone die
// ersten zwei, und eine Gitarre an Eingang 1 käme mit einem leeren Nachbarn
// gemittelt 6 dB leiser an.
//
// Die Pegel zeigen die aktiven Kanäle.
let meterTimer = null;
const meterFills = new Map();
const meterShown = new Map();

function inputChannelRow (page) {
  const chans = devices.inputChannels;
  const active = chans.filter((c) => c.active).map((c) => c.index);

  // Jeder Kanal einzeln, dazu die Paare 1+2, 3+4, …
  const options = chans.map((c) => [[c.index], c.name]);
  for (let i = 0; i + 1 < chans.length; i += 2)
    options.push([[chans[i].index, chans[i + 1].index], `${i + 1} + ${i + 2} STEREO`]);

  const currentKey = active.join(',');
  const pick = async (indices) => {
    const reply = await call('audio.inputChannels', { channels: indices });
    if (!reply?.ok) showToast(reply?.error || 'That did not work.', 'error');
    refreshDevices();
  };

  const r = row(page, 'Input',
    'One channel is mono: the guitar goes to both sides of the chain. Two are stereo.');

  if (options.length <= 4) {
    pills(r.control, options.map(([idx, label]) => [idx.join(','), label]), currentKey,
      (key) => pick(key.split(',').map(Number)));
  } else {
    const labels = options.map(([, label]) => label);
    const current = options.find(([idx]) => idx.join(',') === currentKey)?.[1] ?? currentKey;
    dropdown(r.control, labels, current,
      (label) => pick(options.find(([, l]) => l === label)[0]));
  }

  if (!active.length) return;
  const levels = row(page, 'Input level', 'Play: the channel your guitar is on moves.');
  const stack = el('div', 'level-bars', levels.control);
  meterFills.clear();
  for (const i of active) {
    const meter = el('div', 'level-meter well channel-meter', stack);
    meterFills.set(i, el('div', 'level-meter-fill', meter));
  }
  startInputMeters();
}

function startInputMeters () {
  if (meterTimer) return;
  call('audio.meter', { on: true });
  meterTimer = setInterval(async () => {
    const [d] = await tryAsk('audio.levels');
    if (!meterTimer || !d?.levels) return;
    for (const [i, fill] of meterFills) {
      // Anstieg sofort, Abfall langsam, damit kurze Anschläge sichtbar bleiben.
      const db = d.levels[i] ?? -120;
      const shown = Math.max(db, (meterShown.get(i) ?? -120) - 2);
      meterShown.set(i, shown);
      fill.style.setProperty('--v', pegelSkala(10 ** (shown / 20)));
    }
  }, 60);
}

function stopInputMeters () {
  if (!meterTimer) return;
  clearInterval(meterTimer);
  meterTimer = null;
  meterShown.clear();
  call('audio.meter', { on: false });
}

async function refreshDevices () {
  const [d] = await tryAsk('audio.devices');
  devices = d;
  render();
}

// TONE3000
function pageT3k (page) {
  el('div', 'settings-title', page).textContent = 'TONE3000';
  el('div', 'settings-intro', page).textContent =
    'Sign in once, then captures come straight into the program. '
    + 'Nothing is uploaded.';

  const r = row(page, 'Account', null);
  const dot = el('div', 'status-dot', r.control);
  dot.dataset.on = store.t3k?.connected ? 'true' : 'false';

  const name = el('span', '', r.control);
  name.style.fontSize = '11px';
  name.style.color = 'var(--ink-dim)';
  name.textContent = store.t3k?.connected ? '@' + store.t3k.user : 'not connected';

  if (!store.t3k?.hasKey) {
    el('div', 'setting-hint', r.querySelector('.setting-label')).textContent =
      'This build was made without a TONE3000 key.';
    return;
  }

  if (store.t3k.connected) {
    button(r.control, 'SIGN OUT', async () => {
      await call('t3k.disconnect');
      await resync();
      const { refreshDrawer } = await import('./drawer.js');
      refreshDrawer();
      render();
    }, { danger: true });
  } else {
    const b = button(r.control, 'CONNECT', async () => {
      b.textContent = 'WAITING…';
      const reply = await call('t3k.connect');
      await resync();
      const { refreshDrawer } = await import('./drawer.js');
      refreshDrawer();
      if (!reply?.ok && reply?.error !== 'canceled')
        showToast(reply?.error || 'That did not work.', 'error');
      render();
    });
  }
}

// GENERAL SETTINGS
function pageGeneral (page) {
  el('div', 'settings-title', page).textContent = 'GENERAL SETTINGS';
  // Beide Zeilen betreffen den nächsten Start und stehen deshalb zusammen.
  el('div', 'settings-intro', page).textContent = 'What happens on the next start.';

  const s = store.settings || {};

  const start = row(page, 'On start',
    'The input starts muted as a guard against feedback, not a state '
    + 'anyone sits in for long.');
  pills(start.control, [[0, 'ASK'], [1, 'STAY MUTED'], [2, 'GO LIVE']], s.onStart,
    async (v) => { await call('settings.set', { onStart: v }); await resync(); render(); });

}

// GLOBAL EQ
function pageEq (page) {
  el('div', 'settings-title', page).textContent = 'GLOBAL EQ';
  el('div', 'settings-intro', page).textContent =
    'It describes your monitoring, not the rig: too much bass on headphones is '
    + 'too much on every preset. So it does not belong in a preset and stays '
    + 'put when you switch.';

  const s = store.settings || {};

  const onRow = row(page, 'Global EQ', null);
  pills(onRow.control, [[true, 'ON'], [false, 'OFF']], !!s.eqEnabled,
    async (v) => { await call('eq.enable', { on: v }); await resync(); render(); });

  const curveRow = row(page, 'Curve', null);
  const pick = el('button', 'text-button', curveRow.control);
  pick.textContent = s.eqCurveName || 'CUSTOM';
  pick.addEventListener('click', (e) => {
    const r = pick.getBoundingClientRect();
    showMenu(r.left, r.bottom + 4, (s.eqCurves || []).map((c) => ({
      label: c.name,
      checked: c.name === s.eqCurveName,
      onPick: async () => { await call('eq.select', { name: c.name }); await resync(); render(); },
      // Eigene Kurven speichern und löschen.
      onDelete: c.factory ? null : async () => {
        await call('eq.delete', { name: c.name }); await resync(); render();
      },
    })));
  });

  button(curveRow.control, 'SAVE AS…', async () => {
    const name = await showDialog({ title: 'SAVE CURVE', input: '', placeholder: 'Name',
                                    confirm: 'SAVE' });
    if (!name) return;
    await call('eq.save', { name });
    await resync();
    render();
  });

  // Hinweistext in der Zeile, zu der er gehört.
  const hint = el('div', 'setting-hint', curveRow.querySelector('.setting-label'));
  let deleteButton = null;

  // Sobald an der Kurve gezogen wird, steht dort CUSTOM. Der Name muss sich
  // während des Ziehens ändern, deshalb wird nur diese Zeile neu gebaut, nicht
  // die ganze Seite (das würde den Zug unterbrechen).
  const refreshCurveRow = () => {
    const now = store.settings || {};
    pick.textContent = now.eqCurveName || 'CUSTOM';

    const description =
      (now.eqCurves || []).find((c) => c.name === now.eqCurveName)?.description || '';
    hint.textContent = description;
    hint.hidden = !description;

    // Delete nur bei einer eigenen Kurve. Nach einem Zug heißt die Kurve
    // CUSTOM, dann verschwindet der Knopf, sonst löschte er die vorige.
    const userCurve = (now.eqCurves || [])
      .find((c) => c.name === now.eqCurveName && !c.factory);

    deleteButton?.remove();
    deleteButton = null;
    if (userCurve)
      deleteButton = button(curveRow.control, 'DELETE', async () => {
        await call('eq.delete', { name: userCurve.name });
        await resync();
        render();
      }, { danger: true });
  };
  refreshCurveRow();

  buildEqEditor(page, s, async () => { await resync(); refreshCurveRow(); });
}

// MIDI
function pageMidi (page) {
  el('div', 'settings-title', page).textContent = 'MIDI';

  const s = store.settings || {};
  const clock = store.meters?.clock;

  // INPUT (nur Standalone) steht oben, ohne offenen Eingang wirkt nichts
  // darunter.
  if (store.engine?.standalone && devices?.midiInputs?.length) {
    el('div', 'settings-section', page).textContent = 'INPUT';
    for (const d of devices.midiInputs) {
      const r = row(page, d.name, null);
      pills(r.control, [[true, 'ON'], [false, 'OFF']], d.enabled,
        async (v) => { await call('audio.midiInput', { id: d.id, enabled: v }); refreshDevices(); });
    }
    if (devices.canPairBluetooth) {
      const r = row(page, 'Wireless pedal', 'Pair a wireless pedal.');
      button(r.control, 'PAIR', () => call('audio.pairBluetooth'));
    }
  } else if (!store.engine?.standalone) {
    el('div', 'settings-intro', page).textContent = 'In a plugin the host decides the input.';
  }

  // Taktanzeige, damit man sieht, ob überhaupt eine Clock ankommt.
  const clockRow = row(page, 'Clock', null);
  const clockText = el('span', '', clockRow.control);
  clockText.style.fontSize = '11px';
  clockText.style.color = 'var(--ink-dim)';
  clockText.textContent = clock?.source === 'host' ? `Tempo from host · ${clock.bpm.toFixed(1)} BPM`
                        : clock?.source === 'midi' ? `MIDI clock · ${clock.bpm.toFixed(1)} BPM`
                        : 'No clock';

  el('div', 'settings-section', page).textContent = 'CHANNEL';

  // CHANNEL: Omni oder 1 … 16 als Auswahl.
  const chRow = row(page, 'MIDI channel', 'Clock and transport ignore it.');
  const chDef = def('midi_channel');
  dropdown(chRow.control, chDef?.choices || ['OMNI'],
    chDef?.choices?.[Math.round(paramValue('midi_channel'))] || 'OMNI',
    (v) => {
      const i = chDef.choices.indexOf(v);
      setParamUnit('midi_channel', i / (chDef.choices.length - 1));
    });

  const pcRow = row(page, 'Program change',
    'Program 1 loads the first preset, program 2 the second, in list '
    + 'order. A change over MIDI is shown on screen.');
  pills(pcRow.control, [[true, 'ON'], [false, 'OFF']], paramValue('midi_program_change') >= .5,
    (v) => setParamUnit('midi_program_change', v ? 1 : 0).then(render));

  const thruRow = row(page, 'Thru',
    'Off by default: a plugin that forwards everything unasked makes the '
    + 'next device react to messages meant for this one.');
  pills(thruRow.control, [[true, 'ON'], [false, 'OFF']], paramValue('midi_thru') >= .5,
    (v) => setParamUnit('midi_thru', v ? 1 : 0).then(render));

  // ASSIGNMENTS. Zuweisungen stehen am Fußschalter selbst und unter MIDI · ALL
  // in der Fußleiste, nicht hier.
  el('div', 'settings-section', page).textContent = 'ASSIGNMENTS';
  const assignRow = row(page, 'Footswitches and knobs',
    'Assign them in the middle: click a footswitch in the bar below the card, '
    + 'or MIDI · ALL there for every assignment. Knobs learn with a right-click.');
  button(assignRow.control, 'SHOW', async () => {
    closeSettings();
    const { showMidi } = await import('./rig.js');
    showMidi();
  });

  // SYNC TO CLOCK
  el('div', 'settings-section', page).textContent = 'SYNC TO CLOCK';

  // Ohne externe Clock gedimmt.
  const noClock = !clock || clock.source === 'none';

  // Drums und Looper erst, wenn ihr Raum (TOOLS) freigegeben ist.
  const toolsOpen = !document.querySelector('#rooms button[data-room="tools"]')?.disabled;
  const syncRows = [
    ['sync_delay', 'Delay', 'sync_delay_div'],
    ['sync_tremolo', 'Tremolo', 'sync_tremolo_div'],
    ...(toolsOpen ? [['sync_drums', 'Drums', null], ['sync_looper', 'Looper', null]] : []),
  ];

  for (const [id, name, divId] of syncRows) {
    const r = row(page, name,
      id === 'sync_delay'
        ? 'They live here, not on the cards: four extra switches would each crowd '
          + 'something out.'
        : null,
      { disabled: noClock });

    pills(r.control, [[true, 'ON'], [false, 'OFF']], paramValue(id) >= .5,
      (v) => setParamUnit(id, v ? 1 : 0).then(render));

    if (divId) {
      const d = def(divId);
      dropdown(r.control, d?.choices || [], d?.choices?.[Math.round(paramValue(divId))],
        (v) => {
          const i = d.choices.indexOf(v);
          setParamUnit(divId, i / (d.choices.length - 1));
        });
    }
  }
}

// LIBRARY
function pageLibrary (page) {
  el('div', 'settings-title', page).textContent = 'LIBRARY';

  const s = store.settings || {};

  const pathRow = row(page, 'Where the files live', s.libraryPath);
  button(pathRow.control, `OPEN ${s.libraryFolderName || 'FOLDER'}`,
    () => call('library.reveal'));

  const versionRow = row(page, 'Version', s.version);

  // Vier mögliche Ergebnisse. Die letzten beiden lauten verschieden, sonst sähe
  // ein leerer Server wie eine Bestätigung aus.
  const updateRow = row(page, 'Check for a newer version',
    'Only when you ask. Reaching out to the network on open is nothing anyone '
    + 'asked for, and in a host it would happen once per open instance.');

  const result = el('span', '', updateRow.control);
  result.style.fontSize = '11px';
  result.style.color = 'var(--ink-dim)';

  button(updateRow.control, 'CHECK', async () => {
    result.textContent = 'checking…';
    const reply = await call('settings.checkUpdate');

    if (!reply?.ok) { result.textContent = 'No network.'; result.style.color = 'var(--warn)'; return; }

    const outcome = reply.data.result;
    result.style.color = 'var(--ink-dim)';
    result.textContent =
      outcome === 'offline'        ? 'No network.'
    : outcome === 'newer'          ? `A newer version is out: ${reply.data.latest}`
    : outcome === 'current'        ? 'You are up to date.'
    : /* none-published */           'Nothing has been published yet.';
  });
}
