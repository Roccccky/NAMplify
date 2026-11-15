// Bilder für die Schublade.
//
// Die Bildquelle ist langsam, und der Browser hält je Host nur wenige
// Verbindungen offen. Damit beim langen Scrollen weiter Bilder kommen:
// - höchstens vier Anfragen gleichzeitig,
// - zuletzt angefordert zuerst (das ist, was gerade sichtbar ist),
// - was aus dem Bild gescrollt ist, wird verworfen.

import { call } from './bridge.js';

const MAX_IN_FLIGHT = 4;

const queue = [];            // Stapel: hinten = zuletzt angefordert
const inFlight = new Set();
const done = new Map();      // url → dataURI | null (null = geht nicht)

export function requestImage (url, node, isVisible) {
  if (!url || !node) return;

  if (done.has(url)) { apply(node, done.get(url)); return; }

  // Doppelte Anfragen für dieselbe Adresse zusammenlegen.
  const existing = queue.find((e) => e.url === url);
  if (existing) {
    existing.nodes.add(node);
    existing.isVisible = isVisible;
    // Nach hinten: zuletzt angefordert zuerst.
    queue.splice(queue.indexOf(existing), 1);
    queue.push(existing);
    return;
  }

  queue.push({ url, nodes: new Set([node]), isVisible });
  pump();
}

function apply (node, uri) {
  if (!uri) return;
  const img = node.querySelector('img');
  if (!img) return;
  img.src = uri;
  img.dataset.loaded = 'true';
  // Das Platzhalterzeichen tritt zurück, sobald ein Bild da ist.
  node.dataset.image = 'true';
}

function pump () {
  while (inFlight.size < MAX_IN_FLIGHT && queue.length) {
    // Zuletzt angefordert zuerst.
    const entry = queue.pop();

    // Anfragen für Zeilen verwerfen, die aus dem Bild gescrollt sind.
    if (entry.isVisible && !entry.isVisible()) continue;

    inFlight.add(entry.url);

    call('t3k.image', { url: entry.url }).then((reply) => {
      inFlight.delete(entry.url);
      const uri = reply?.ok ? reply.data.uri : null;
      done.set(entry.url, uri);
      for (const node of entry.nodes) apply(node, uri);
      pump();
    }).catch(() => {
      inFlight.delete(entry.url);
      done.set(entry.url, null);
      pump();
    });
  }
}

export function clearImageQueue () {
  queue.length = 0;
}
