// TONE3000-Browser.

import { el } from './controls.js';
import { icon, slotIcon } from './icons.js';
import { tone3000Wordmark } from './tone3000-logo.js';
import { store, notify } from './store.js';
import { selectedSlot } from './rig.js';
import { call, on, tryAsk } from './bridge.js';
import { showToast, showMenu } from './dialogs.js';
import { requestImage, clearImageQueue } from './imageQueue.js';
import { invalidateLibrary } from './wheel.js';

/// Die Kategorien bei TONE3000 und wohin sie laden. Die Farbe ist die des
/// Slots, der sie aufnimmt, sie kommt vom Programm.
const CATEGORIES = [
  { id: 0, name: 'ALL' },
  { id: 1, name: 'AMP',      type: 'NAM', role: 'amp' },
  { id: 2, name: 'AMP+CAB', type: 'NAM', role: 'fullrig' },   // so heißt es bei TONE3000
  { id: 3, name: 'IR',       type: 'IR' },
  { id: 4, name: 'PEDALS',   type: 'NAM', role: 'pedal' },
  { id: 5, name: 'OUTBOARD', type: 'NAM', role: 'outboard' },
  { id: 6, name: 'SPACES',   type: 'NAM', role: 'space' },      // Räume und Hall, bei TONE3000 space
];

function colourFor (type, role) {
  if (type === 'NAM') return store.roleByKey.get(role)?.colour || 'var(--ink-dim)';
  return store.typeByKey.get(type)?.colour || 'var(--ink-dim)';
}

function categoryForSlot (n) {
  const s = store.slots[n];
  if (!s) return 0;
  if (s.type === 'IR') return 3;
  if (s.type !== 'NAM') return 0;
  return { amp: 1, fullrig: 2, pedal: 4, outboard: 5, space: 6 }[s.role] ?? 0;
}

const SORTS = [
  ['trending', 'TRENDING'],
  ['newest', 'NEWEST'],
  ['downloads-all-time', 'DOWNLOADS'],
];


const state = {
  open: true,
  mode: 'library',     // 'library' oder 't3k', die zwei Reiter oben
  libText: '',         // Suche in der eigenen Bibliothek
  libCategory: null,   // null = folgt dem gewählten Slot
  libOpen: new Set(),
  tab: 0,
  category: 0,
  text: '',
  sort: 'trending',
  view: 'list',
  page: 1,
  totalPages: 1,
  rows: [],
  loading: false,
  loadingMore: false,
  error: null,
  generation: 0,
  expanded: new Set(),
  variants: new Map(),
  busy: null,          // { tone, model }
  progress: new Map(), // "tone:model" → 0…1 oder -1
  bank: null,          // { tone, done, total }, eine ganze Bank wird geholt
};

let body = null, listEl = null, headEl = null;

// Aufbau. Der Browser ist eine Karte rechts und immer offen.
let tabsEl = null;

export function initDrawer () {
  body = document.getElementById('drawer-body');
  const drawer = document.getElementById('drawer');
  tabsEl = el('div', 'browser-tabs');
  drawer.insertBefore(tabsEl, body);

  on('t3kProgress', (p) => {
    const key = `${p.tone}:${p.model}`;
    if (p.done) state.progress.delete(key);
    else state.progress.set(key, p.value ?? -1);
    paintProgress();
  });

  renderTabs();
  render();
}

function renderTabs () {
  if (!tabsEl) return;
  tabsEl.innerHTML = '';
  tabsEl.parentElement.dataset.mode = state.mode;
  // Zwei Reiter: TONE3000 und die eigene Bibliothek. Darunter steht jeweils nur
  // das Werkzeug des Reiters.
  const t3k = el('button', 'browser-tab', tabsEl);
  t3k.appendChild(tone3000Wordmark());
  t3k.title = 'Search TONE3000';
  t3k.dataset.on = state.mode === 't3k' ? 'true' : 'false';
  t3k.addEventListener('click', () => setMode('t3k'));

  const lib = el('button', 'browser-tab', tabsEl);
  lib.appendChild(icon('books'));
  el('span', '', lib).textContent = 'LIBRARY';
  lib.title = 'Your library: everything downloaded or imported, also offline';
  lib.dataset.on = state.mode === 'library' ? 'true' : 'false';
  lib.addEventListener('click', () => setMode('library'));

  // Angemeldet als @name, sichtbar ohne Aufklappen.
  if (store.t3k?.connected && store.t3k.user)
    el('span', 'browser-user', tabsEl).textContent = '@' + store.t3k.user;
}

function setMode (mode) {
  state.mode = mode;
  renderTabs();
  render();
  if (mode === 't3k' && store.t3k?.connected && !state.rows.length) search(true);
}

export function toggleDrawer () { /* der Browser steht immer offen */ }

/// Nur Favoriten: ein Herz neben der Suche. Es grenzt die Suche ein wie
/// Kategorie und Text.
function renderFavSwitch (parent) {
  const b = el('button', 'drawer-fav', parent);
  b.appendChild(icon('heart'));
  const on = state.tab === 2;
  b.dataset.on = on ? 'true' : 'false';
  b.title = on ? 'Showing your favourites. Click for all of TONE3000' : 'Only your favourites';
  b.addEventListener('click', () => {
    state.tab = on ? 0 : 2;
    state.page = 1;
    render();
    search(true);
  });
  return b;
}

/// Aufruf aus der Auswahlliste (ALL) und von einer leeren Karte: TONE3000 mit
/// dem Filter dieses Slots.
export function openBrowserFor (n) {
  state.category = categoryForSlot(n);
  state.page = 1;
  state.mode = 't3k';
  renderTabs();
  render();
  if (store.t3k?.connected) search(true);
}

export function updateTabLabel () { renderTabs(); }

// Zeichnen
function render () {
  if (!body) return;
  body.innerHTML = '';

  const inner = el('div', 'drawer-inner', body);
  if (state.mode === 'library') { renderLibrary(inner); return; }

  // Ohne Anmeldung
  if (!store.t3k?.connected) {
    const stateBox = el('div', 'drawer-state', inner);

    if (!store.t3k?.hasKey) {
      stateBox.dataset.kind = 'error';
      el('div', 'drawer-state-title', stateBox).textContent = 'NO TONE3000 KEY';
      el('div', '', stateBox).textContent =
        'This build was made without a TONE3000 key. '
        + 'Rebuild with -DNAMPLIFY_T3K_CLIENT_ID=t3k_pub_…';
      return;
    }

    // TONE3000-Wortlogo über dem nächsten Schritt.
    const logo = el('div', 'drawer-t3k-logo', stateBox);
    logo.appendChild(tone3000Wordmark());

    // Statt einer leeren Liste der nächste Schritt.
    el('div', 'drawer-state-title', stateBox).textContent = 'Connect to TONE3000';
    el('div', '', stateBox).textContent =
      'Sign in once, then thousands of captures come straight into the program. '
      + 'Nothing is uploaded.';

    const button = el('button', 'drawer-button', stateBox);
    button.dataset.accent = 'true';
    button.textContent = 'CONNECT';

    const reason = el('div', '', stateBox);
    reason.style.color = 'var(--error)';

    button.addEventListener('click', async () => {
      button.textContent = 'WAITING FOR BROWSER…';
      reason.textContent = '';
      const reply = await call('t3k.connect');
      if (reply?.ok) {
        const { resync } = await import('./store.js');
        await resync();
        renderTabs();
        render();
        search(true);
      } else {
        // Scheitert es, steht der Grund darunter. Hat der Nutzer im Browser
        // selbst abgebrochen, steht nichts da.
        button.textContent = 'CONNECT';
        reason.textContent = reply?.error === 'canceled' ? ''
                           : (reply?.error || 'That did not work.');
      }
    });
    return;
  }

  // Kopfzeile
  headEl = el('div', 'drawer-head', inner);

  // Alle Werkzeuge in einer Zeile: Reiter, Suche, Sortierung, Ansicht.
  const tools = el('div', 'drawer-tools', headEl);

  // Die Suche steht vorn, die Schalter dahinter grenzen sie ein.
  const input = el('input', 'drawer-search', tools);
  input.placeholder = 'Search…';
  input.value = state.text;
  // Enter sucht.
  input.addEventListener('keydown', (e) => {
    if (e.key !== 'Enter') return;
    state.text = input.value.trim();
    state.page = 1;
    search(true);
  });

  renderFavSwitch(tools);

  const searchRow = tools;

  // Sortierung als aufklappende Liste, damit man die Auswahl sieht und nicht
  // jeder Schritt eine Suche auslöst.
  const sortBox = el('div', 'drawer-sort', searchRow);
  const sortButton = el('button', 'drawer-sort-button', sortBox);
  const sortLabel = el('span', '', sortButton);
  sortLabel.textContent = SORTS.find((s) => s[0] === state.sort)?.[1] || 'TRENDING';
  sortButton.appendChild(icon('chevronDown'));
  sortButton.addEventListener('click', (e) => {
    const r = sortButton.getBoundingClientRect();
    showMenu(r.left, r.bottom + 4, SORTS.map(([id, label]) => ({
      label, checked: state.sort === id,
      onPick: () => { state.sort = id; state.page = 1; render(); search(true); },
    })));
  });

  const viewToggle = el('div', 'drawer-view-toggle', searchRow);
  [['list', 'list'], ['shelf', 'grid']].forEach(([id, ic]) => {
    const b = el('button', 'drawer-view-btn', viewToggle);
    b.appendChild(icon(ic));
    b.dataset.on = state.view === id ? 'true' : 'false';
    b.addEventListener('click', () => { state.view = id; render(); paintRows(); });
  });

  const cats = el('div', 'drawer-cats', headEl);
  for (const c of CATEGORIES) {
    const b = el('button', 'drawer-cat', cats);
    b.textContent = c.name;
    b.style.setProperty('--cat-colour', c.type ? colourFor(c.type, c.role) : 'var(--ink-dim)');
    b.dataset.on = state.category === c.id ? 'true' : 'false';
    b.addEventListener('click', () => {
      state.category = c.id; state.page = 1; render(); search(true);
    });
  }

  // Liste
  listEl = el('div', 'drawer-list', inner);
  listEl.dataset.view = state.view;

  // Seitenweise und vorausschauend nachladen, damit die Liste ohne Stocken
  // durchläuft.
  listEl.addEventListener('scroll', () => {
    const near = listEl.scrollTop + listEl.clientHeight > listEl.scrollHeight - 400;
    if (near && !state.loading && !state.loadingMore && state.page < state.totalPages)
      search(false);

    const atEnd = listEl.scrollTop + listEl.clientHeight > listEl.scrollHeight - 8;
    const edge = document.getElementById('drawer-edge');
    if (edge) edge.dataset.end = (atEnd && state.page >= state.totalPages) ? 'true' : 'false';

    paintImages();
  }, { passive: true });

  paintRows();
}

// Suche
async function search (fresh) {
  if (fresh) { state.rows = []; state.page = 1; state.expanded.clear(); state.variants.clear(); }

  const generation = ++state.generation;

  if (fresh) { state.loading = true; state.error = null; }
  else       { state.loadingMore = true; }
  paintRows();

  const reply = await call('t3k.search', {
    tab: state.tab, category: state.category, text: state.text,
    sort: state.sort, page: fresh ? 1 : state.page + 1,
  });

  // Antworten können sich überholen. Es gilt nur die Antwort auf die letzte
  // Suche.
  if (generation !== state.generation) return;

  state.loading = false;
  state.loadingMore = false;

  if (!reply?.ok) {
    state.error = reply?.error === 'not-connected'
      ? null : (reply?.error || 'That did not work.');
    if (reply?.error === 'not-connected') { render(); return; }
    paintRows();
    return;
  }

  state.error = null;
  state.page = reply.data.page;
  state.totalPages = reply.data.totalPages;
  state.rows = fresh ? reply.data.rows : state.rows.concat(reply.data.rows);
  paintRows();
  // Das Programm filtert jede Seite nach, eine Seite kann fast leer ankommen.
  // Dann gleich die nächste holen, bis die Liste scrollt.
  if (listEl && state.page < state.totalPages
      && listEl.scrollHeight <= listEl.clientHeight + 40) search(false);
}

// Zeilen
function paintRows () {
  if (!listEl) return;
  listEl.innerHTML = '';
  listEl.dataset.view = state.view;

  // Nie zwei Ladeanzeigen gleichzeitig. Eine neue Suche zeigt eine Anzeige
  // mitten in der leeren Liste, der Fuß gehört dem Nachladen.
  if (state.loading) { stateLine('LOADING…', null); return; }

  if (state.error) {
    // Laden, keine Treffer und Fehler erscheinen gleich: eine Zeile in der
    // Mitte, Fehler in Rot.
    stateLine(state.error, 'error');
    return;
  }

  if (!state.rows.length) { stateLine('NOTHING FOUND', null); return; }

  for (const row of state.rows) {
    listEl.appendChild(state.view === 'shelf' ? buildCard(row) : buildRow(row));

    if (state.expanded.has(row.id)) {
      const box = el('div', 't3k-variants');
      const variants = state.variants.get(row.id);

      if (!variants) {
        // Die Liste öffnet sofort, der Spinner dreht darin. In der Pille sähe
        // man nichts passieren und klickte noch einmal.
        const spin = el('div', 't3k-spinner', box);
        spin.appendChild(icon('spinner'));
      } else {
        for (const v of variants) box.appendChild(buildVariant(row, v));
      }
      listEl.appendChild(box);
    }
  }

  // Der Spinner steht als letzte Zeile und erscheint erst nach kurzer
  // Verzögerung.
  if (state.loadingMore) {
    const spin = el('div', 't3k-spinner', listEl);
    spin.style.opacity = '0';
    spin.appendChild(icon('spinner'));
    setTimeout(() => { spin.style.opacity = '1'; }, 260);
  }

  paintImages();
  paintProgress();
}

function stateLine (text, kind) {
  const box = el('div', 'drawer-state', listEl);
  if (kind) box.dataset.kind = kind;
  el('div', '', box).textContent = text;
}

function rowColour (row) {
  return colourFor(row.type, row.role);
}

function buildRow (row) {
  const node = el('div', 't3k-row');
  node.dataset.tone = row.id;
  node.style.setProperty('--row-colour', rowColour(row));
  node.dataset.here = isOnBoard(row) ? 'true' : 'false';

  el('div', 't3k-led', node);

  const thumb = el('div', 't3k-thumb', node);
  const ic = el('div', 't3k-thumb-icon', thumb);
  ic.appendChild(slotIcon(row.type, row.role));
  const img = document.createElement('img');
  img.alt = '';
  img.crossOrigin = 'anonymous';
  thumb.appendChild(img);
  thumb.dataset.url = row.image || '';

  const main = el('div', 't3k-main', node);
  el('div', 't3k-title', main).textContent = row.title;

  const meta = el('div', 't3k-meta', main);
  const cat = el('span', 't3k-meta-cat', meta);
  cat.textContent = (row.format === 'ir' ? 'IR'
                   : row.gear === 'amp-cab' ? 'AMP+CAB' : row.gear).toUpperCase();
  el('span', '', meta).textContent = '@' + row.author;
  el('span', '', meta).textContent = '↓' + row.downloads;

  // n VAR klappt die Varianten auf.
  const varPill = el('button', 't3k-var', node);
  varPill.dataset.slot = 'var';
  varPill.textContent = `${row.models} VAR`;
  varPill.addEventListener('click', (e) => {
    e.stopPropagation();
    toggleVariants(row);
  });

  node.appendChild(buildHold(row, null));

  // Klick auf die Zeile lädt zur Vorschau: die Datei landet sofort im passenden
  // Slot, ohne in die Bibliothek zu gehen. Liegt sie schon dort, wird sie
  // normal geladen.
  node.addEventListener('click', () => {
    if (row.here) loadFromLibrary(row);
    else fetchRow(row, null, false);
  });

  return node;
}

function buildCard (row) {
  const node = el('div', 't3k-card');
  node.dataset.tone = row.id;
  node.style.setProperty('--row-colour', rowColour(row));

  const thumb = el('div', 't3k-thumb', node);
  const ic = el('div', 't3k-thumb-icon', thumb);
  ic.appendChild(slotIcon(row.type, row.role));
  const img = document.createElement('img');
  img.alt = ''; img.crossOrigin = 'anonymous';
  thumb.appendChild(img);
  thumb.dataset.url = row.image || '';

  el('div', 't3k-title', node).textContent = row.title;

  const bottom = el('div', 't3k-card-row', node);
  const led = el('div', 't3k-led', bottom);
  led.style.background = isOnBoard(row) ? rowColour(row) : '';
  el('div', 't3k-meta', bottom).textContent = '@' + row.author;
  bottom.appendChild(buildHold(row, null));

  node.addEventListener('click', () => {
    if (row.here) loadFromLibrary(row); else fetchRow(row, null, false);
  });
  return node;
}

function buildVariant (row, v) {
  const node = el('div', 't3k-row t3k-variant');
  node.dataset.tone = row.id;
  node.dataset.model = v.id;
  node.style.setProperty('--row-colour', rowColour(row));
  // Was lokal liegt, ist heller, entschieden je Datei, nicht je Bank.
  node.dataset.here = v.here ? 'true' : 'false';

  el('div', 't3k-led', node);

  const main = el('div', 't3k-main', node);
  el('div', 't3k-title', main).textContent = v.name;
  const meta = el('div', 't3k-meta', main);
  el('span', '', meta).textContent = v.size?.toUpperCase() || '';
  if (v.architecture) el('span', '', meta).textContent = 'A' + v.architecture;

  const slot = el('div', 't3k-var', node);
  slot.dataset.slot = 'var';
  slot.style.visibility = 'hidden';

  node.appendChild(buildHold(row, v));

  // Klick auf die Zeile: liegt die Datei hier, laden, sonst Vorschau.
  node.addEventListener('click', () => {
    if (v.here) loadFromLibrary(row, v);
    else fetchRow(row, v, false);
  });

  return node;
}

/// Haltekasten: Pfeil = holen. Liegt es hier, steht das Laufwerk darin, unter
/// dem Zeiger wird daraus der Mülleimer. Der Tooltip sagt, was der Klick tut.
function buildHold (row, variant) {
  const here = variant ? variant.here : isInLibrary(row);
  const hold = el('button', 't3k-hold');
  hold.dataset.mode = here ? 'delete' : 'get';
  hold.appendChild(icon(here ? 'drive' : 'download'));
  hold.title = here ? 'Delete this bank' : 'Get it';

  hold.addEventListener('pointerenter', () => {
    if (!here) return;
    hold.innerHTML = '';
    hold.appendChild(icon('trash'));
  });
  hold.addEventListener('pointerleave', () => {
    if (!here) return;
    hold.innerHTML = '';
    hold.appendChild(icon('drive'));
  });

  hold.addEventListener('click', async (e) => {
    e.stopPropagation();

    if (here) {
      const reply = await call('t3k.delete', { tone: row.id });
      if (!reply?.ok) { showToast(reply?.error || 'Could not delete.', 'error'); return; }

      // Nach dem Löschen wird oben wieder der Pfeil daraus, und alle
      // Variantenzeilen darunter ziehen mit.
      row.here = false;
      const vs = state.variants.get(row.id);
      if (vs) for (const v of vs) v.here = false;

      // In der eigenen Bibliothek verschwindet die Zeile samt Varianten. In der
      // Suche bleibt sie, beim Dienst gibt es sie weiter.
      if (state.tab === 1) state.rows = state.rows.filter((r) => r.id !== row.id);

      invalidateLibrary();
      const { resync } = await import('./store.js');
      await resync();
      paintRows();
      return;
    }

    // Am Knopf einer Bank hängt die ganze Bank, an dem einer Variante nur die
    // eine Datei.
    if (variant) fetchRow(row, variant, true);
    else         fetchBank(row);
  });

  return hold;
}

// Holen
async function fetchRow (row, variant, store_) {
  // Es lädt immer nur eine Zeile.
  if (state.busy) { showToast('Something is already loading.', 'warn'); return false; }

  state.busy = { tone: row.id, model: variant?.id || 0 };
  const key = `${row.id}:${variant?.id || row.id * 10}`;
  state.progress.set(key, -1);
  paintProgress();

  const reply = await call(store_ ? 't3k.get' : 't3k.audition', {
    tone: row.id, model: variant?.id || 0, slot: selectedSlot(),
    title: row.title, gear: row.gear, format: row.format, author: row.author, page: row.url || '', image: row.image,
  });

  state.busy = null;
  state.progress.delete(key);

  if (!reply?.ok) {
    if (reply?.error !== 'busy') showToast(reply?.error || 'That did not work.', 'error');
    paintProgress();
    return false;
  }

  if (store_) {
    row.here = true;
    if (variant) variant.here = true;
  }

  invalidateLibrary();
  const { resync } = await import('./store.js');
  await resync();
  paintRows();
  return true;
}

/// Eine ganze Bank holen: die Bank klappt auf, die Varianten laden
/// nacheinander, jede mit eigenem Balken, oben an der Bank der
/// Gesamtfortschritt.
async function fetchBank (row) {
  if (state.busy || state.bank) { showToast('Something is already loading.', 'warn'); return; }

  if (!state.expanded.has(row.id)) { state.expanded.add(row.id); paintRows(); }

  let list = state.variants.get(row.id);
  if (!list) {
    const [data] = await tryAsk('t3k.models', { tone: row.id, format: row.format });
    list = data?.rows || [];
    state.variants.set(row.id, list);
    paintRows();
  }

  const todo = list.filter((v) => !v.here);
  if (!todo.length) { showToast('Everything from this bank is already here.', 'warn'); return; }

  state.bank = { tone: row.id, done: 0, total: todo.length };
  paintProgress();

  for (const v of todo) {
    if (!await fetchRow(row, v, true)) break;      // Fehler wird schon angezeigt
    state.bank.done += 1;
    paintProgress();
  }

  state.bank = null;
  paintRows();
}

async function loadFromLibrary (row, variant) {
  const [banks] = await tryAsk('library.list', { type: row.type, role: row.role });
  const bank = (banks || []).find((b) => b.toneId === row.id || b.title === row.title);
  if (!bank) { showToast('Not found in the library.', 'warn'); return; }

  const v = variant
    ? bank.variants.find((x) => x.modelId === variant.id) || bank.variants[0]
    : bank.variants[0];

  await loadInto(row.type, row.role, bank, v);
}

/// Lädt eine Variante auf den gewählten Slot, wenn er passt, sonst auf einen
/// passenden oder neuen (slot.place).
async function loadInto (type, role, bank, v) {
  const [placed, error] = await tryAsk('slot.place', { type, role, slot: selectedSlot() });
  if (!placed) { showToast(error || 'There is no room for it.', 'warn'); return; }
  await call('slot.load', { slot: placed.slot, path: v.path, bank: bank.title,
                            variant: v.name, gear: bank.gear });
  const { selectSlot } = await import('./rig.js');
  selectSlot(placed.slot);
}

// Bibliothek: was schon auf der Platte liegt, nach Kategorie. Die Kategorie
// folgt dem gewählten Slot, bis man selbst eine wählt.
async function renderLibrary (inner) {
  const head = el('div', 'drawer-head', inner);
  const tools = el('div', 'drawer-tools', head);
  // Suche wie bei TONE3000, aber sofort beim Tippen, weil alles lokal ist.
  const input = el('input', 'drawer-search', tools);
  input.placeholder = 'Search…';
  input.value = state.libText;
  input.addEventListener('input', () => { state.libText = input.value; fillLibrary(list); });
  const cats = el('div', 'drawer-cats', head);
  const current = state.libCategory ?? (categoryForSlot(selectedSlot()) || 1);

  for (const c of CATEGORIES.slice(1)) {
    const b = el('button', 'drawer-cat', cats);
    b.textContent = c.name;
    b.style.setProperty('--cat-colour', colourFor(c.type, c.role));
    b.dataset.on = current === c.id ? 'true' : 'false';
    b.addEventListener('click', () => { state.libCategory = c.id; render(); });
  }

  const list = el('div', 'lib-list', inner);
  await fillLibrary(list);
}

async function fillLibrary (list) {
  const current = state.libCategory ?? (categoryForSlot(selectedSlot()) || 1);
  const cat = CATEGORIES[current];
  const generation = ++state.generation;
  const [all] = await tryAsk('library.list', { type: cat.type, role: cat.role || 'amp' });
  if (generation !== state.generation) return;
  list.innerHTML = '';

  const q = state.libText.trim().toLowerCase();
  const banks = (all || []).filter((b) => !q || b.title.toLowerCase().includes(q)
                                        || (b.author || '').toLowerCase().includes(q)
                                        || b.variants.some((v) => v.name.toLowerCase().includes(q)));

  if (q && !banks.length && all?.length) {
    el('div', 'drawer-state-title', el('div', 'drawer-state', list)).textContent = 'NOTHING MATCHES';
    return;
  }

  if (!banks?.length) {
    const box = el('div', 'drawer-state', list);
    el('div', 'drawer-state-title', box).textContent = 'NOTHING HERE YET';
    const get = el('button', 'drawer-button', box);
    get.dataset.accent = 'true';
    get.textContent = 'GET FROM TONE3000';
    get.addEventListener('click', () => { state.category = current; state.tab = 0; setMode('t3k'); search(true); });
    return;
  }

  const colour = colourFor(cat.type, cat.role);
  const loaded = new Set(store.slots.filter((s) => s.file && !s.file.empty)
                                    .map((s) => s.file.bank + '\u0000' + s.file.variant));
  const loadedBanks = new Set(store.slots.filter((s) => s.file && !s.file.empty).map((s) => s.file.bank));

  for (const bank of banks) {
    const row = el('button', 'lib-bank', list);
    row.style.setProperty('--row-colour', colour);
    row.dataset.loaded = loadedBanks.has(bank.title) ? 'true' : 'false';
    const thumb = el('div', 'lib-thumb', row);
    if (bank.image) thumb.style.backgroundImage = `url("${bank.image}")`;
    const main = el('div', '', row);
    el('div', 'lib-title', main).textContent = bank.title;
    el('div', 'lib-meta', main).textContent =
      `${bank.variants.length} variant${bank.variants.length === 1 ? '' : 's'}`
      + (bank.author ? ` · @${bank.author}` : '');

    const open = state.libOpen.has(bank.path);
    row.addEventListener('click', () => {
      // Klick auf die Bank lädt ihre erste Variante und klappt die übrigen auf.
      if (!open) { state.libOpen.add(bank.path); loadInto(cat.type, cat.role, bank, bank.variants[0]); }
      else state.libOpen.delete(bank.path);
      fillLibrary(list);
    });

    if (open) {
      const vs = el('div', 'lib-variants', list);
      for (const v of bank.variants) {
        const b = el('button', 'lib-variant', vs);
        b.style.setProperty('--row-colour', colour);
        b.textContent = v.name;
        b.dataset.loaded = loaded.has(bank.title + '\u0000' + v.name) ? 'true' : 'false';
        b.addEventListener('click', () => loadInto(cat.type, cat.role, bank, v));
      }
    }
  }
}

async function toggleVariants (row) {
  if (state.expanded.has(row.id)) { state.expanded.delete(row.id); paintRows(); return; }

  state.expanded.add(row.id);
  paintRows();     // sofort auf, Spinner darin

  const [data] = await tryAsk('t3k.models', { tone: row.id, format: row.format });
  state.variants.set(row.id, data?.rows || []);
  paintRows();
}

// Anzeigen
function isInLibrary (row) { return !!row.here; }

/// LED vorn: leuchtet, wenn genau diese Datei auf dem Brett liegt.
function isOnBoard (row) {
  return store.slots.some((s) => s.file && !s.file.empty && s.file.bank === row.title);
}

function paintImages () {
  if (!listEl) return;
  const box = listEl.getBoundingClientRect();

  for (const thumb of listEl.querySelectorAll('.t3k-thumb')) {
    const url = thumb.dataset.url;
    if (!url) continue;
    const r = thumb.getBoundingClientRect();
    const visible = r.bottom > box.top - 300 && r.top < box.bottom + 300;
    if (!visible) continue;

    requestImage(url, thumb, () => {
      const rr = thumb.getBoundingClientRect();
      const bb = listEl.getBoundingClientRect();
      return rr.bottom > bb.top - 400 && rr.top < bb.bottom + 400;
    });
  }
}

/// Während eine Zeile lädt, pulsiert die LED, und an der Stelle der VAR-Pille
/// läuft ein Balken.
function paintProgress () {
  if (!listEl) return;

  for (const node of listEl.querySelectorAll('.t3k-row')) {
    const tone = Number(node.dataset.tone);
    const model = node.dataset.model ? Number(node.dataset.model) : null;
    const key = model !== null ? `${tone}:${model}` : null;

    let value;
    if (key !== null) {
      value = state.progress.get(key);
    } else if (state.bank && state.bank.tone === tone) {
      // Der Balken der Bank zählt Dateien, nicht Bytes: fertige plus der Anteil
      // der laufenden.
      let running = 0;
      for (const [k, v] of state.progress)
        if (k.startsWith(tone + ':')) { running = v > 0 ? v : 0; break; }
      value = Math.min(1, (state.bank.done + running) / Math.max(1, state.bank.total));
    } else {
      for (const [k, v] of state.progress) if (k.startsWith(tone + ':')) { value = v; break; }
    }

    const busy = value !== undefined;
    node.dataset.busy = busy ? 'true' : 'false';

    const slot = node.querySelector('[data-slot="var"]');
    if (!slot) continue;

    if (busy) {
      if (!slot.classList.contains('t3k-progress')) {
        slot.className = 't3k-progress';
        slot.innerHTML = '';
        el('div', 't3k-progress-fill', slot);
      }
      slot.dataset.unknown = value < 0 ? 'true' : 'false';
      const fill = slot.querySelector('.t3k-progress-fill');
      if (fill && value >= 0) fill.style.width = `${value * 100}%`;
      slot.style.visibility = 'visible';
    } else if (slot.classList.contains('t3k-progress')) {
      slot.className = 't3k-var';
      slot.dataset.slot = 'var';
      slot.innerHTML = '';
      slot.style.visibility = model !== null ? 'hidden' : 'visible';
    }
  }
}

/// STORE auf der Karte hat ein Vorschau-Capture abgelegt: Zeile und Variante
/// zeigen jetzt, dass es lokal liegt.
export function markStored (tone, model) {
  for (const row of state.rows) if (row.id === tone) row.here = true;
  for (const v of state.variants.get(tone) || []) if (v.id === model) v.here = true;
  paintRows();
}

export function refreshDrawer () {
  renderTabs();
  if (state.mode === 'library') render();
  else paintRows();
}
