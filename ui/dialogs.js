// Toasts, Menüs und Rückfragen.
//
// Alle Dialoge zeichnet die UI selbst. In eingebetteten WebViews beantworten
// Systemdialoge sich teils selbst, deshalb kein alert, confirm oder prompt.

import { el } from './controls.js';
import { icon } from './icons.js';

// Toast
let toastTimer = null;

export function showToast (text, kind = 'info', ms = 4200) {
  const slot = document.getElementById('toast-slot');
  if (!slot) return;

  // Eine neue Meldung ersetzt die alte.
  slot.innerHTML = '';
  clearTimeout(toastTimer);

  const node = el('div', 'toast', slot);
  node.dataset.kind = kind;
  node.textContent = text;

  // Per Timer statt Animation, damit sie auch verschwindet, wenn das Fenster
  // zwischendurch nicht gezeichnet wurde.
  toastTimer = setTimeout(() => { node.remove(); }, ms);
}

// Menü
let menuCleanup = null;

export function showMenu (x, y, items, { minWidth = 0 } = {}) {
  const menu = document.getElementById('overlay-menu');
  if (!menu) return;

  closeMenu();
  menu.innerHTML = '';
  menu.style.minWidth = minWidth ? `${Math.round(minWidth)}px` : '';

  for (const item of items) {
    if (item.separator) { el('div', 'menu-sep', menu); continue; }

    // Abschnittsüberschrift in der Liste.
    if (item.heading) { el('div', 'menu-heading', menu).textContent = item.heading; continue; }

    if (item.custom) { menu.appendChild(item.custom); continue; }

    if (item.empty) {
      el('div', 'menu-empty', menu).textContent = item.label;
      continue;
    }

    const row = el('button', 'menu-item', menu);

    if (item.checked !== undefined) {
      const mark = el('span', 'menu-item-mark', row);
      if (item.checked) mark.appendChild(icon('check'));
      if (item.checked) row.dataset.current = 'true';
    } else if (item.icon) {
      row.appendChild(icon(item.icon));
    }

    // Bildchen vor dem Namen: das Gehäuse des Typs oder das Bild des Captures.
    if (item.picture !== undefined) {
      const pic = el('span', 'menu-item-picture', row);
      if (typeof item.picture === 'string') pic.style.backgroundImage = `url("${item.picture}")`;
      else if (item.picture) pic.appendChild(item.picture);
    }

    const label = el('span', 'menu-item-label', row);
    label.textContent = item.label;
    label.style.flex = '1';

    // Mülleimer unter dem Zeiger, löscht sofort.
    if (item.onDelete) {
      const trash = el('span', 'menu-item-trash', row);
      trash.appendChild(icon('trash'));
      trash.addEventListener('click', (e) => {
        e.stopPropagation();
        item.onDelete();
        closeMenu();
      });
    }

    // Was gerade nicht geht, steht gedimmt da, mit dem Grund im Titel.
    if (item.disabled) {
      row.dataset.disabled = 'true';
      row.style.opacity = '.38';
      if (item.title) row.title = item.title;
      continue;
    }
    if (item.hint) {
      const h = el('span', 'menu-item-hint', row);
      h.textContent = item.hint;
      h.style.opacity = '.5';
      h.style.fontSize = '10px';
    }

    row.addEventListener('click', () => { closeMenu(); item.onPick?.(); });
  }

  menu.hidden = false;
  menu.style.left = '0px';
  menu.style.top = '0px';

  const r = menu.getBoundingClientRect();
  const left = Math.min(x, window.innerWidth - r.width - 8);
  const top = Math.min(y, window.innerHeight - r.height - 8);
  menu.style.left = `${Math.max(8, left)}px`;
  menu.style.top = `${Math.max(8, top)}px`;
  // Eine lange Liste öffnet beim geladenen Eintrag.
  menu.querySelector('[data-current="true"]')?.scrollIntoView({ block: 'center' });

  const onDown = (e) => { if (!menu.contains(e.target)) closeMenu(); };
  const onKey = (e) => { if (e.key === 'Escape') closeMenu(); };

  setTimeout(() => {
    window.addEventListener('pointerdown', onDown, true);
    window.addEventListener('keydown', onKey, true);
  }, 0);

  menuCleanup = () => {
    window.removeEventListener('pointerdown', onDown, true);
    window.removeEventListener('keydown', onKey, true);
  };
}

export function closeMenu () {
  const menu = document.getElementById('overlay-menu');
  if (menu) menu.hidden = true;
  menuCleanup?.();
  menuCleanup = null;
}

// Rückfrage
/// Selbst gezeichnet, mit oder ohne Textfeld. Abbruch mit ESC oder Klick
/// daneben.
///
/// Mit suggest(variant) gibt es im Textfeld einen Zauberstab, der Vorschläge
/// einsetzt. suggestFrom ist der erste Vorschlag.
export function showDialog ({ title, text, input, placeholder, confirm = 'OK',
                              cancel = 'CANCEL', note, danger = false,
                              suggest = null, suggestFrom = 0 }) {
  return new Promise((resolve) => {
    const overlay = document.getElementById('overlay-dialog');
    overlay.innerHTML = '';
    overlay.hidden = false;

    const scrim = el('div', 'overlay-scrim', overlay);
    const dialog = el('div', 'dialog', overlay);

    if (title) el('div', 'dialog-title', dialog).textContent = title;
    if (text) el('div', 'dialog-text', dialog).innerHTML = text;

    let field = null;
    if (input !== undefined) {
      const wrap = suggest ? el('div', 'dialog-field', dialog) : dialog;
      field = el('input', 'dialog-input', wrap);
      field.value = input || '';
      field.placeholder = placeholder || '';
      setTimeout(() => { field.focus(); field.select(); }, 30);

      if (suggest) {
        let variant = suggestFrom;
        const wand = el('button', 'dialog-suggest', wrap);
        wand.type = 'button';
        wand.title = 'Suggest a name';
        wand.appendChild(icon('wand'));
        // Der Fokus bleibt im Feld, Enter speichert danach direkt.
        wand.addEventListener('pointerdown', (e) => e.preventDefault());
        wand.addEventListener('click', () => {
          field.value = suggest(variant);
          variant += 1;
          field.focus();
          field.select();
        });
      }
    }

    if (note) el('div', 'dialog-note', dialog).textContent = note;

    const buttons = el('div', 'dialog-buttons', dialog);

    const close = (value) => {
      window.removeEventListener('keydown', onKey, true);
      overlay.hidden = true;
      overlay.innerHTML = '';
      resolve(value);
    };

    if (cancel) {
      const b = el('button', 'text-button', buttons);
      b.textContent = cancel;
      b.addEventListener('click', () => close(null));
    }

    const okButton = el('button', 'text-button', buttons);
    okButton.textContent = confirm;
    if (danger) okButton.dataset.danger = 'true';
    okButton.addEventListener('click', () => close(field ? field.value.trim() : true));

    const onKey = (e) => {
      if (e.key === 'Escape') { e.stopPropagation(); close(null); }
      if (e.key === 'Enter' && field) { e.stopPropagation(); close(field.value.trim()); }
    };
    window.addEventListener('keydown', onKey, true);

    scrim.addEventListener('click', () => close(null));
  });
}
