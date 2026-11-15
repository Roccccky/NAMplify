// Startabfrage: stumm bleiben, einrichten oder einschalten.

import { el } from './controls.js';
import { icon } from './icons.js';
import { store } from './store.js';
import { call } from './bridge.js';

// Im selben Stil wie das Programm, kein Systemdialog.
export async function askOnStart () {
  // Im Plugin gibt es die Abfrage nicht. Dort bestimmt der Host, was am Eingang
  // liegt.
  if (!store.engine?.standalone) return;

  const mode = store.settings?.onStart ?? 0;
  if (mode === 1) { await call('param.set', { id: 'mute', unit: 1 }); return; }
  if (mode === 2) { await call('param.set', { id: 'mute', unit: 0 }); return; }

  const overlay = document.getElementById('overlay-dialog');
  overlay.innerHTML = '';
  overlay.hidden = false;

  const scrim = el('div', 'overlay-scrim', overlay);
  const dialog = el('div', 'dialog', overlay);

  el('div', 'dialog-title', dialog).textContent = 'INPUT';

  const text = el('div', 'dialog-text', dialog);
  text.innerHTML = '<b>The input starts muted.</b> Go live once your guitar, '
                 + 'interface and monitoring are ready, otherwise it feeds straight back.';

  let dontAsk = false;

  const buttons = el('div', 'dialog-buttons', dialog);

  const finish = async (choice) => {
    overlay.hidden = true;
    overlay.innerHTML = '';

    if (choice === 'live') await call('param.set', { id: 'mute', unit: 0 });
    else await call('param.set', { id: 'mute', unit: 1 });

    // Nur mit Don't ask again wird die Wahl gespeichert. SET UP wird nie
    // gespeichert, sonst käme man nicht mehr hierher.
    if (dontAsk && choice !== 'setup')
      await call('settings.set', { onStart: choice === 'live' ? 2 : 1 });

    if (choice === 'setup') {
      const { openSettings } = await import('./settings.js');
      openSettings();
    }
  };

  const stay = el('button', 'text-button', buttons);
  stay.textContent = 'STAY MUTED';
  stay.addEventListener('click', () => finish('muted'));

  const setup = el('button', 'text-button', buttons);
  setup.textContent = 'SET UP';
  setup.addEventListener('click', () => finish('setup'));

  const live = el('button', 'text-button', buttons);
  live.textContent = 'GO LIVE';
  live.addEventListener('click', () => finish('live'));

  const check = el('div', 'start-check', dialog);
  const box = el('div', 'start-box', check);
  box.appendChild(icon('check'));
  el('span', '', check).textContent = "Don't ask again";
  check.addEventListener('click', () => {
    dontAsk = !dontAsk;
    check.dataset.on = dontAsk ? 'true' : 'false';
  });

  // Klick daneben bleibt stumm.
  scrim.addEventListener('click', () => finish('muted'));
}
