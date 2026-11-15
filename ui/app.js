// Verdrahtung der UI.

import { icon } from './icons.js';
import { el } from './controls.js';
import { store, resync, subscribe, notify, paramOn, setParamUnit } from './store.js';
import { call, on, isEmbedded } from './bridge.js';
import { initRig, renderRig, updateRig, rigMeters } from './rig.js';
import { initTools, renderTools, updateTools, toolsMeters } from './tools.js';
import { initDrawer, refreshDrawer } from './drawer.js';
import { renderTuner, updateTuner, openTuner, closeTuner } from './tuner.js';
import { openSettings, closeSettings, isSettingsOpen } from './settings.js';
import { askOnStart } from './start.js';
import { showMenu, showDialog, showToast } from './dialogs.js';
import { invalidateLibrary } from './wheel.js';
import { suggestPresetName } from './preset-name.js';

let cpuFill = null, cpuValue = null;
/// Geglättet, damit die Zahl lesbar bleibt: nach oben schnell, nach unten
/// langsam.
let cpuShown = 0;
let lastStructure = -1;

// Kopfleiste
function buildHeader () {
  document.getElementById('btn-tuner').appendChild(icon('tuningFork'));
  document.getElementById('btn-settings').appendChild(icon('gear'));

  // Das Stimmgerät gehört zum Brett und öffnet im Raum RIG, auch von TOOLS aus.
  document.getElementById('btn-tuner').addEventListener('click', () => {
    if (paramOn('tuner')) { closeTuner(); return; }
    if (room !== 'rig') setRoom('rig');
    openTuner();
  });

  // Der einzige Weg zu den Einstellungen.
  document.getElementById('btn-settings').addEventListener('click', () => openSettings());

  document.getElementById('btn-go-live').addEventListener('click', () =>
    setParamUnit('mute', 0).then(() => notify('mute')));

  buildPresetDisplay();
  buildRooms();
  buildCpu();
}

// CPU-Anzeige oben rechts neben Stimmgerät und Einstellungen: eine Pille mit
// Namen, die Auslastung als Zahl daneben, dazu ein Balken.
function buildCpu () {
  const right = document.querySelector('.header-right');
  const g = el('div', 'header-cpu');
  right.prepend(g);
  g.title = 'How hard the computer is working';
  const meter = el('div', 'level-meter well', g);
  cpuFill = el('div', 'level-meter-fill', meter);
  el('div', 'level-meter-label', meter).textContent = 'CPU';
  cpuValue = el('div', 'desk-readout', g);
  cpuValue.textContent = '—';
}

// Räume: RIG und TOOLS als Reiter neben der Wortmarke. Der Raum gehört zum
// Fenster, nicht ins Preset. Ein gesperrter Raum (TOOLS, solange Looper und Co.
// nicht fertig sind) öffnet weder per Klick noch aus dem gespeicherten Stand.
const roomOpen = (name) =>
  !document.querySelector(`#rooms button[data-room="${name}"]`)?.disabled;

let room = (() => {
  try { return localStorage.getItem('namplify-room') === 'tools' ? 'tools' : 'rig'; } catch { return 'rig'; }
})();

function buildRooms () {
  if (!roomOpen(room)) room = 'rig';
  for (const b of document.querySelectorAll('#rooms button'))
    b.addEventListener('click', () => setRoom(b.dataset.room));
  paintRooms();
}

function setRoom (next) {
  if (!roomOpen(next)) return;
  room = next;
  try { localStorage.setItem('namplify-room', next); } catch { /* nur Bequemlichkeit */ }
  paintRooms();
  if (room === 'tools') renderTools(); else renderRig();
}

function paintRooms () {
  for (const b of document.querySelectorAll('#rooms button'))
    b.dataset.on = b.dataset.room === room ? 'true' : 'false';
  document.querySelector('.workspace').hidden = room !== 'rig';
  document.getElementById('tools-room').hidden = room !== 'tools';
  fitCentreOverlays();
}

// Overlays in der Mitte: Stimmgerät und Einstellungen öffnen über Kette und
// Karte, Presets und Browser bleiben ausgegraut stehen. Ein Klick ins Graue
// schließt.
const CENTRE_OVERLAYS = ['overlay-tuner', 'overlay-settings'];

function fitCentreOverlays () {
  const centre = document.querySelector(room === 'tools' ? '#tools-room .center' : '.workspace .center');
  if (!centre) return;
  let any = false;
  for (const id of CENTRE_OVERLAYS) {
    const o = document.getElementById(id);
    if (!o) continue;
    o.classList.add('in-centre');
    if (o.hidden) continue;
    any = true;
    // Das Stimmgerät liegt nur über der Karte, Kette und Fußleiste bleiben
    // sichtbar. Die Einstellungen nehmen die ganze Mitte.
    const card = id === 'overlay-tuner' ? document.getElementById('slot-card') : null;
    const r = (card && room === 'rig' ? card : centre).getBoundingClientRect();
    Object.assign(o.style, { left: `${r.left}px`, top: `${r.top}px`,
                             width: `${r.width}px`, height: `${r.height}px` });
  }
  document.body.dataset.focus = any ? 'centre' : '';
}

function initCentreOverlays () {
  const watch = new MutationObserver(fitCentreOverlays);
  for (const id of CENTRE_OVERLAYS)
    watch.observe(document.getElementById(id), { attributes: true, attributeFilter: ['hidden'] });
  window.addEventListener('resize', fitCentreOverlays);
  // Klick ins Graue: schließen, sonst nichts.
  document.addEventListener('click', (e) => {
    if (document.body.dataset.focus !== 'centre') return;
    if (!e.target.closest('.workspace > aside, .side-col > aside, .tools-room > aside, #preset-display, #rooms')) return;
    e.preventDefault();
    e.stopPropagation();
    if (paramOn('tuner')) closeTuner();
    if (isSettingsOpen()) closeSettings();
  }, true);
  fitCentreOverlays();
}

// Presets: ◀ Name ▶ mittig in der Kopfleiste. Die Pfeile blättern, am Ende
// umlaufend, ein Klick auf den Namen zeigt die Liste.
let presetName = null;

// Klick auf den Namen öffnet die Liste direkt darunter, so breit wie die
// Anzeige, mit dem geladenen Eintrag mittig. Keine Mausrad-Vorschau, sonst
// wechselt der Klang beim Darüberscrollen.
//
// Daneben NEW und SAVE. SAVE wird rot, solange das Brett ungespeicherte
// Änderungen hat.
function buildPresetDisplay () {
  const host = document.getElementById('preset-display');
  host.innerHTML = '';
  const display = el('div', 'screen-display', host);

  let list = null;

  const names = () => store.presets?.list || [];
  const current = () => names().indexOf(store.presets?.current);

  const load = async (i) => {
    const name = names()[i];
    if (name && name !== store.presets?.current) { await call('preset.load', { name }); invalidateLibrary(); await refresh(); }
  };

  const closeList = () => { list?.remove(); list = null; };

  const arrow = (dir) => {
    const b = el('button', 'screen-display-arrow', display);
    b.dataset.dir = dir < 0 ? 'left' : 'right';
    b.title = dir < 0 ? 'Previous preset' : 'Next preset';
    b.addEventListener('click', async () => { closeList(); await call('preset.step', { dir }); invalidateLibrary(); await refresh(); });
    return b;
  };

  arrow(-1);
  const value = el('button', 'screen-display-value', display);
  value.title = 'Click for the list';
  el('b', '', value).textContent = 'PRESET';
  presetName = el('span', '', value);
  arrow(1);

  value.addEventListener('click', () => {
    if (list) { closeList(); return; }
    list = el('div', 'screen-dial screen-dial-list', display);
    list.setAttribute('role', 'listbox');
    const all = names();
    if (!all.length) el('div', 'screen-dial-empty', list).textContent = 'No presets yet';
    all.forEach((name, i) => {
      const row = el('button', 'screen-dial-row', list);
      row.setAttribute('role', 'option');
      row.dataset.on = i === current() ? 'true' : 'false';
      el('b', '', row).textContent = String(i + 1).padStart(3, '0');
      el('span', '', row).textContent = name;
      row.addEventListener('click', () => { closeList(); load(i); });
    });
    list.querySelector('[data-on="true"]')?.scrollIntoView({ block: 'center' });
  });

  value.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeList(); });

  // Klick daneben schließt die Liste.
  window.addEventListener('pointerdown', (e) => { if (list && !display.contains(e.target)) closeList(); });

  // NEW, SAVE
  const buttons = el('div', 'header-rig-buttons', host);
  const newBtn = el('button', 'hs-btn', buttons);
  newBtn.title = 'New preset: an empty chain';
  newBtn.append(icon('plus'), document.createTextNode('NEW'));
  newBtn.addEventListener('click', async () => {
    const answer = await showDialog({
      title: 'NEW PRESET',
      text: 'Start from an empty chain. What is not saved is lost.',
      confirm: 'NEW', cancel: 'CANCEL',
    });
    if (!answer) return;
    await call('preset.reset');
    invalidateLibrary();
    await refresh();
  });

  saveBtn = el('button', 'hs-btn', buttons);
  saveBtn.title = 'Save the board as a preset';
  saveBtn.append(icon('save'), document.createTextNode('SAVE'));
  saveBtn.addEventListener('click', async () => {
    // Ein neues Brett bekommt gleich einen Namensvorschlag, der Zauberstab
    // blättert weiter (preset-name.js).
    const current = store.presets?.current || '';
    const name = await showDialog({
      title: 'SAVE PRESET',
      text: 'A preset covers the whole board: slots, order, every knob and the loaded files.',
      input: current || suggestPresetName(0), placeholder: 'Name', confirm: 'SAVE',
      suggest: suggestPresetName, suggestFrom: current ? 0 : 1,
    });
    if (!name) return;
    // Vorschau-Captures legt das Programm dabei in die Bibliothek, die
    // Schublade zeigt sie dann als lokal vorhanden.
    const reply = await call('preset.save', { name });
    const kept = reply?.data?.kept || [];
    if (kept.length) {
      const { markStored } = await import('./drawer.js');
      for (const k of kept) markStored(k.tone, k.model);
      invalidateLibrary();
      showToast(kept.length === 1 ? 'The preview is now in your library.'
                                  : `${kept.length} previews are now in your library.`);
    }
    if (reply?.data?.notKept)
      showToast('A preview from an older version could not be stored. Get it from the TONE3000 list.', 'warn');
    await refresh();
  });
}
let saveBtn = null;

function paintPresetDisplay () {
  if (!presetName) return;
  const current = store.presets?.current || '';
  presetName.textContent = current || 'Unsaved board';
  presetName.dataset.empty = current ? 'false' : 'true';
  if (saveBtn) saveBtn.dataset.dirty = store.presets?.dirty || !current ? 'true' : 'false';
}

/// --scale ist fest 1,2. Regler, Fußschalter und Schriften der Karten wachsen
/// damit (Ring 34 px, Beschriftung 9–10 px).
function applyScale () {
  document.documentElement.style.setProperty('--scale', '1.2');
}

// Auffrischen
async function refresh () {
  await resync();
  paintAll();
}

function paintAll () {
  if (room === 'rig') renderRig(); else renderTools();
  paintHeader();
  renderTuner();
  refreshDrawer();

  paintPresetDisplay();
}

function paintHeader () {
  const muted = paramOn('mute');
  const tunerOpen = paramOn('tuner');

  // Nicht, solange das Stimmgerät offen ist. Dort ist stumm normal und hat
  // einen eigenen Taster.
  document.getElementById('mute-banner').hidden = !muted || tunerOpen;

  document.getElementById('btn-tuner').dataset.active = tunerOpen ? 'true' : 'false';
}

// Meter
function paintMeters (m) {
  if (!m) return;

  if (cpuFill) {
    const raw = Math.min(1, Math.max(0, m.cpu));
    // Nach oben sofort, nach unten langsam.
    cpuShown = raw > cpuShown ? raw : cpuShown + (raw - cpuShown) * .08;
    cpuFill.style.setProperty('--v', cpuShown);
    // Ab etwa 90 % warm, wie die Pegel oben.
    const warm = cpuShown > 0.9 ? 'true' : 'false';
    cpuFill.dataset.warm = warm;
    if (cpuValue) {
      cpuValue.textContent = `${Math.round(cpuShown * 100)} %`;
      cpuValue.dataset.warm = warm;
    }
  }

  updateTuner(m);
  if (room === 'rig') rigMeters(m); else toolsMeters(m);
}

// Start
async function main () {
  // Erst den Zustand holen, dann bauen: die Bedienelemente lesen Bereich, Name
  // und Werkswert aus der Beschreibung des Programms.
  await resync();

  buildHeader();
  applyScale();
  initCentreOverlays();
  initRig({ refresh });
  initTools({ refresh });
  initDrawer();

  await refresh();
  lastStructure = store.structure ?? 0;


  on('meters', paintMeters);
  on('notice', (n) => showToast(n.text, n.kind || 'info'));
  on('structure', async () => { invalidateLibrary(); await refresh(); });

  subscribe((what) => {
    if (what === 'meters') return;
    if (room === 'rig') updateRig(); else updateTools();
    paintHeader();
    if (what === 'tuner') {
      // Auch wenn es von außen geöffnet wird (MIDI), gehört es in den Raum RIG.
      if (paramOn('tuner') && room !== 'rig') setRoom('rig');
      renderTuner();
    }
  });

  // Angehaltene Übergänge nachholen, sobald das Fenster wieder sichtbar ist.
  document.addEventListener('visibilitychange', async () => {
    const visible = document.visibilityState === 'visible';
    call('visible', { visible });
    if (!visible) return;
    await refresh();
  });

  window.addEventListener('focus', () => call('visible', { visible: true }));

  // Tastatur: ESC schließt, was offen ist.
  window.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    if (paramOn('tuner')) { closeTuner(); return; }
  });

  // Erster Start: nur die Frage nach dem Eingang. Der erste Start wird
  // vermerkt, sonst legte das Programm die mitgelieferten Boxen bei jedem Start
  // neu ab.
  if (store.firstRun) call('firstrun.done');
  askOnStart();
}

main().catch((e) => {
  console.error(e);
  document.body.innerHTML =
    `<div style="padding:40px;font:13px/1.7 sans-serif;color:#e2574c">`
    + `NAMplify could not start:<br><br>${e.message}</div>`;
});
