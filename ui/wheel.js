// Bänke und Presets eines Slots: blättern und aus der Liste wählen.

import { el } from './controls.js';
import { store, slot as slotInfo, slotColour } from './store.js';
import { call, ask, tryAsk } from './bridge.js';
import { showDialog, showMenu } from './dialogs.js';
import { openBrowserFor } from './drawer.js';

let cache = new Map();      // "TYP:rolle" → banks

/// Die Bänke, die auf einen Slot passen. Sie hängen an Typ und Rolle, nicht an
/// der Slotnummer, zwei Amps teilen sie sich.
const keyFor = (n) => { const s = slotInfo(n); return `${s?.type}:${s?.role || ''}`; };

export async function loadLibrary (n, force = false) {
  const key = keyFor(n);
  if (!force && cache.has(key)) return cache.get(key);
  const [banks] = await tryAsk('library.list', { slot: n });
  cache.set(key, banks || []);
  return cache.get(key);
}

export function invalidateLibrary () { cache.clear(); }

function currentBank (n, banks) {
  const slot = slotInfo(n)?.file;
  if (!slot || slot.empty) return null;
  // Über den Namen wiederfinden, nicht über die Position. Sonst verrutscht
  // alles, wenn ein Capture dazukommt oder wegfällt.
  return banks.find((b) => b.title === slot.bank) || null;
}

function currentVariant (n, bank) {
  const slot = slotInfo(n)?.file;
  if (!bank || !slot) return null;
  return bank.variants.find((v) => v.name === slot.variant) || null;
}

// Blättern
export async function stepLibrary (n, line, dir) {
  const banks = await loadLibrary(n);
  if (!banks.length) return;

  const bank = currentBank(n, banks);

  if (line === 'bank') {
    const i = bank ? banks.indexOf(bank) : -1;
    // Am Ende umlaufend.
    const next = banks[((i + dir) % banks.length + banks.length) % banks.length];
    // Eine Bank wählen lädt ihr erstes Preset.
    return loadVariant(n, next, next.variants[0]);
  }

  if (!bank) return;
  const variants = bank.variants;
  const v = currentVariant(n, bank);
  const i = v ? variants.indexOf(v) : -1;
  const next = variants[((i + dir) % variants.length + variants.length) % variants.length];
  return loadVariant(n, bank, next);
}

async function loadVariant (n, bank, variant) {
  if (!bank || !variant) return;
  await call('slot.load', {
    slot: n,
    path: variant.path,
    bank: bank.title,
    variant: variant.name,
    gear: bank.gear,
  });
}

// Öffnen: eine normale Liste unter der Zeile. Bild und Name, der geladene
// Eintrag markiert und mittig, Mülleimer unter dem Zeiger.
export async function openPicker (n, line, anchorEl) {
  const banks = await loadLibrary(n, true);

  // Ist nichts geladen, öffnet der Klick die Schublade mit dem Filter dieser
  // Karte.
  const slot = slotInfo(n)?.file;
  if (line === 'bank' && slot?.empty && banks.length === 0) { openBrowserFor(n); return; }

  const bank = currentBank(n, banks);
  if (line === 'preset' && !bank) { openBrowserFor(n); return; }

  const items = line === 'bank'
    ? banks.map((b) => ({ key: b.title, name: b.title, image: b.image, bank: b,
                          variant: b.variants[0], deletable: true, path: b.path }))
    : bank.variants.map((v) => ({ key: v.name, name: v.name, image: bank.image, bank,
                                  variant: v, deletable: bank.variants.length > 1,
                                  path: v.path }));

  const currentKey = line === 'bank'
    ? (bank?.title ?? null)
    : (currentVariant(n, bank)?.name ?? null);

  // Hat irgendein Eintrag ein Bild, bekommt jede Zeile den Platz dafür, damit
  // die Liste bündig bleibt.
  const anyImage = items.some((i) => !!i.image);

  const menu = items.map((item) => ({
    label: item.name,
    checked: item.key === currentKey,
    picture: anyImage ? (item.image || null) : undefined,
    onPick: () => loadVariant(n, item.bank, item.variant),
    // Mülleimer rechts an der Zeile, nur wenn die Datei wirklich auf der Platte
    // liegt.
    onDelete: item.deletable ? async () => {
      const cmd = line === 'bank' ? 'library.deleteBank' : 'library.deleteVariant';
      const reply = await call(cmd, { path: item.path });
      if (!reply?.ok) {
        showDialog({ title: 'NOT DELETED', text: reply?.error || 'Unknown error.',
                     cancel: null, confirm: 'OK' });
        return;
      }
      invalidateLibrary();
    } : undefined,
  }));
  if (!menu.length) menu.push({ empty: true, label: 'Nothing here yet' });

  // ALL und + am Ende der Liste: die ganze Sammlung in der Schublade, oder eine
  // eigene Datei.
  menu.push({ separator: true });
  menu.push({ label: 'GET FROM TONE3000…', icon: 'search', onPick: () => openBrowserFor(n) });
  menu.push({ label: 'ADD YOUR OWN FILE…', icon: 'plus', onPick: async () => {
    await call('library.import', { slot: n });
    invalidateLibrary();
  } });

  const r = anchorEl.getBoundingClientRect();
  showMenu(r.left, r.bottom + 4, menu, { minWidth: r.width });
}
