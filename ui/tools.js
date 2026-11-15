// Raum TOOLS: Looper, Beat Machine, Recorder.
//
// Alle drei stehen untereinander als waagerechte Streifen: links Schalter und
// Name, rechts die Bedienung. Looper und Beat Machine bedient man zusammen,
// deshalb keine Auswahlliste.
//
// Klanglich stehen sie am Ende der Kette. Der Schalter sagt, ob sie laufen
// (tool.show). Ausgeschaltet ist die Bedienung gedämpft sichtbar. Anfassen
// schaltet das Werkzeug ein, damit der Looper nicht stumm aufnimmt.
//
// Der Recorder hat vorerst einen eigenen Streifen.

import { el } from './controls.js';
import { store } from './store.js';
import { call } from './bridge.js';
import { cardIcon } from './icons.js';
import { buildToolBody } from './cards.js';

let refreshAll = async () => {};
const strips = new Map();   // key → { shown, parts, meters, apply, update, beats }

export function initTools ({ refresh }) { refreshAll = refresh; }

/// Für Aufrufer, die ein Werkzeug öffnen wollen: es wird ins Bild gescrollt.
export function openTool (key) {
  document.querySelector(`.tool-strip[data-tool="${key}"]`)?.scrollIntoView({ block: 'nearest' });
}

export function renderTools () {
  const room = document.getElementById('tools-room');
  if (!room || room.hidden) return;
  const host = document.getElementById('tool-zone');

  // Nur neu bauen, was sich geändert hat. Der Looper hält Wellenbilder, die
  // sonst bei jedem Regler neu entstünden.
  const keys = store.tools.map((t) => t.key).join('|');
  if (host.dataset.keys !== keys) { host.innerHTML = ''; strips.clear(); host.dataset.keys = keys; }

  for (const t of store.tools) {
    const have = strips.get(t.key);
    if (have) {
      have.shown = !!t.shown;
      have.node.dataset.on = t.shown ? 'true' : 'false';
      have.power.dataset.on = t.shown ? 'true' : 'false';
      have.power.title = t.shown ? 'Switch off' : 'Switch on. It plays at the end of the chain';
      for (const p of have.parts || []) p.render?.(); have.apply?.(); have.update?.();
      continue;
    }
    renderStrip(host, t);
  }
}

export function updateTools () {
  for (const s of strips.values()) { for (const p of s.parts || []) p.render?.(); s.apply?.(); }
}

export function toolsMeters (m) {
  for (const s of strips.values()) {
    s.meters?.(m);
    if (m?.drums && s.beats) s.beats.setFlashes?.(m.drums.flashes);
  }
}

function renderStrip (host, t) {
  const node = el('section', 'panel tool-strip');
  host.appendChild(node);
  node.dataset.tool = t.key;
  node.dataset.on = t.shown ? 'true' : 'false';
  const colour = t.colour || '#888';
  node.style.setProperty('--card-colour', colour);
  const strip = { shown: !!t.shown, node };
  strips.set(t.key, strip);

  // Links: Schalter, Zeichen, Name
  const head = el('div', 'tool-strip-head', node);
  const power = el('button', 'power', head);
  power.dataset.on = t.shown ? 'true' : 'false';
  power.title = t.shown ? 'Switch off' : 'Switch on. It plays at the end of the chain';
  power.addEventListener('click', async () => {
    await call('tool.show', { tool: t.key, shown: !strip.shown });
    await refreshAll();
  });
  strip.power = power;
  const mark = el('div', 'tool-strip-mark', head);
  mark.appendChild(cardIcon(t.key));
  const names = el('div', 'tool-strip-names', head);
  el('div', 'type-button', names).textContent = t.name;
  el('div', 'slot-where', names).textContent = 'END OF CHAIN';

  // Rechts: die Bedienung in einer Reihe
  const fake = {};
  const body = buildToolBody(t.key, fake, colour);
  node.appendChild(body);
  // Anfassen schaltet ein, und zwar zuerst, damit der erste Tritt auf eine
  // Looper-Spur schon im laufenden Werkzeug landet.
  body.addEventListener('pointerdown', () => {
    if (strip.shown) return;
    strip.shown = true;
    call('tool.show', { tool: t.key, shown: true }).then(() => refreshAll());
  }, true);
  Object.assign(strip, { parts: fake.parts || [], meters: fake.meters, apply: fake.apply,
                         update: fake.update, beats: fake.beats });
}
