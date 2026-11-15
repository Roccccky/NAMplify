// Bildchen auf den Kacheln: ein flaches Gehäuse, wie das Gerät auf dem Boden
// aussähe. Treter mit Knöpfen und Fußschalter, Amp-Kopf, Box, IR mit Mikrofon,
// NAM-Kasten mit Wellenlinie.
//
// Eigene Zeichnungen, keine Marken, keine Schrift. Das Gehäuse trägt die Farbe
// des Typs (store.types, store.roles), etwas dunkler als die Kachel.

const NS = 'http://www.w3.org/2000/svg';
const DARK = '#1c1c1c';

/// Aussehen je Gerät: rows = Knopfreihen, plate = Schaltplatte statt
/// Fußschalter, faders = EQ-Schieber.
const ART = {
  GATE:    { shape: 'stomp', rows: [2] },
  COMP:    { shape: 'stomp', rows: [2, 1] },
  DRIVE:   { shape: 'stomp', rows: [3] },
  EQ:      { shape: 'wide',  faders: 7 },
  CHORUS:  { shape: 'stomp', rows: [2], plate: true },
  FLANGER: { shape: 'stomp', rows: [2, 2], plate: true },
  PHASER:  { shape: 'stomp', rows: [1] },
  TREMOLO: { shape: 'stomp', rows: [3] },
  DELAY:   { shape: 'wide',  rows: [4], switches: 2 },
  REVERB:  { shape: 'stomp', rows: [3], plate: true },
  IR:      { shape: 'ir' },
  // NAM-Slot ohne Capture: der Kasten mit der Welle. Die Rolle ergibt sich erst
  // aus der Datei.
  NAM:            { shape: 'nam' },
  'NAM:amp':      { shape: 'amp' },
  'NAM:pedal':    { shape: 'stomp', rows: [2, 1] },
  'NAM:outboard': { shape: 'wide',  rows: [4, 4], switches: 0 },
  // Raum: ein flaches Rackgerät mit wenigen großen Knöpfen.
  'NAM:space':    { shape: 'wide',  rows: [3], switches: 0 },
  'NAM:fullrig':  { shape: 'nam' },
};

const hexRgb = (hex) => {
  const v = parseInt(String(hex).replace('#', '').padEnd(6, '0').slice(0, 6), 16);
  return [v >> 16 & 255, v >> 8 & 255, v & 255];
};
const light = (hex) => { const [r, g, b] = hexRgb(hex); return (0.299 * r + 0.587 * g + 0.114 * b) / 255; };
const shade = (hex, k) => {
  const [r, g, b] = hexRgb(hex).map((c) => Math.round(c * k));
  return `rgb(${r},${g},${b})`;
};

function node (tag, attrs, parent) {
  const n = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) if (v !== undefined && v !== null) n.setAttribute(k, v);
  parent?.appendChild(n);
  return n;
}

function knob (g, x, y, r, colour) {
  node('circle', { class: 'rpa-knob', cx: x, cy: y, r, fill: colour }, g);
  node('line', { class: 'rpa-dot', x1: x, y1: y, x2: x + r * .55, y2: y - r * .72,
                 stroke: light(colour) > .55 ? '#1a1a1a' : '#f2f2f2' }, g);
}

function knobRows (g, rows, a, colour) {
  const pitch = (a.y1 - a.y0) / Math.max(rows.length, 1);
  rows.forEach((count, row) => {
    const y = a.y0 + pitch * (row + .5);
    const w = a.x1 - a.x0;
    const r = Math.min(pitch * .4, count <= 2 ? w * .15 : w / (count * 2.6));
    const inset = count === 2 ? w * .17 : r * 1.2;
    for (let i = 0; i < count; ++i)
      knob(g, count === 1 ? (a.x0 + a.x1) / 2 : a.x0 + inset + (w - 2 * inset) * i / (count - 1), y, r, colour);
  });
}

function faders (g, count, a, body) {
  const ink = light(body) > .55 ? '#1a1a1a' : '#f2f2f2';
  for (let i = 0; i < count; ++i) {
    const x = a.x0 + (a.x1 - a.x0) * (i + .5) / count;
    const at = .25 + .5 * Math.abs(Math.cos(Math.PI * (i + .5) / count));
    const y = a.y0 + (a.y1 - a.y0) * at;
    node('line', { class: 'rpa-slot', x1: x, x2: x, y1: a.y0, y2: a.y1, stroke: ink }, g);
    node('rect', { class: 'rpa-cap', x: x - 1.1, y: y - .7, width: 2.2, height: 1.4, rx: .3, fill: ink }, g);
  }
}

function footswitch (g, x, y, r = 3) {
  node('circle', { class: 'rpa-sw', cx: x, cy: y, r }, g);
  node('circle', { class: 'rpa-sw-cap', cx: x, cy: y, r: r * .55 }, g);
}

const led = (g, x, y) => node('circle', { class: 'rpa-led', cx: x, cy: y, r: .9 }, g);

function speaker (g, x, y, r) {
  node('circle', { class: 'rpa-grille', cx: x, cy: y, r }, g);
  node('circle', { class: 'rpa-cone', cx: x, cy: y, r: r * .62 }, g);
  node('circle', { class: 'rpa-dust', cx: x, cy: y, r: r * .22 }, g);
}

/// Knöpfe, Schieber, Leuchte und Schalter auf einem Gehäuse.
function face (g, art, body, box, switchY, colour) {
  const bottom = art.plate ? box.y0 + (box.y1 - box.y0) * .5 : switchY - 5;
  const top = box.y0 + 2.4;
  const area = { x0: box.x0 + 1.5, x1: box.x1 - 1.5, y0: top, y1: bottom };
  const rows = art.rows ?? [];
  if (art.faders) faders(g, art.faders, area, body);
  else knobRows(g, rows, area, DARK);
  led(g, (rows[0] ?? 0) % 2 ? box.x1 - 3 : (box.x0 + box.x1) / 2, box.y0 + 1.6);
  if (art.plate) {
    node('rect', { class: 'rpa-plate', x: box.x0 + 1.2, y: bottom + 1.4, width: box.x1 - box.x0 - 2.4,
                   height: box.y1 - bottom - 2.6, rx: 1, fill: shade(colour, .38) }, g);
  } else if ((art.switches ?? 1) > 0) {
    const cx = (box.x0 + box.x1) / 2, spread = (box.x1 - box.x0) * .27;
    if ((art.switches ?? 1) === 1) footswitch(g, cx, switchY);
    else { footswitch(g, cx - spread, switchY, 2.5); footswitch(g, cx + spread, switchY, 2.5); }
  }
}

function draw (g, art, colour) {
  const body = shade(colour, .62);
  switch (art.shape) {
    case 'stomp':
      node('rect', { class: 'rpa-body', x: 9.5, y: 3, width: 21, height: 34, rx: 2.2, fill: body }, g);
      face(g, art, body, { x0: 9.5, x1: 30.5, y0: 3, y1: 37 }, 31.5, colour);
      break;
    case 'wide':
      node('rect', { class: 'rpa-body', x: 3.5, y: 7, width: 33, height: 26, rx: 2.2, fill: body }, g);
      face(g, art, body, { x0: 3.5, x1: 36.5, y0: 7, y1: 33 }, 28.5, colour);
      break;
    case 'amp':
      // Kopf: Tolex, Frontplatte in der Farbe, eine Reihe kleiner Knöpfe.
      node('rect', { class: 'rpa-handle', x: 15, y: 8.5, width: 10, height: 2.4, rx: 1 }, g);
      node('rect', { class: 'rpa-body', x: 2.5, y: 11, width: 35, height: 18, rx: 2, fill: DARK }, g);
      node('rect', { x: 4.5, y: 13, width: 31, height: 7, rx: 1, fill: 'rgba(255,255,255,.08)' }, g);
      node('rect', { x: 4.5, y: 21.5, width: 31, height: 5.5, rx: 1, fill: body }, g);
      knobRows(g, [7], { x0: 5, x1: 35, y0: 21.5, y1: 27 }, DARK);
      break;
    case 'ir':
      // Ein Lautsprecher im dunklen Rahmen, davor ein Mikrofon.
      node('rect', { class: 'rpa-body', x: 3, y: 3, width: 34, height: 34, rx: 2, fill: DARK }, g);
      node('rect', { x: 5, y: 5, width: 30, height: 30, rx: 1.2, fill: 'none', stroke: body, 'stroke-width': .8 }, g);
      speaker(g, 20, 21, 11);
      node('path', { class: 'rpa-mic', d: 'M20 4 V13' }, g);
      node('rect', { class: 'rpa-mic-head', x: 18.4, y: 12, width: 3.2, height: 5, rx: 1.4 }, g);
      break;
    case 'nam':
      // Ein dunkler Kasten mit leuchtender Kante und der Welle des Captures.
      node('rect', { class: 'rpa-body', x: 3, y: 11, width: 34, height: 18, rx: 2, fill: DARK,
                     style: `stroke:${colour};stroke-width:1.2` }, g);
      node('path', { d: 'M7 19 C9 14 11 14 12.5 18.5 S16 23.5 18 19 S22 13.5 24 18 S27.5 22.5 29.5 19 S32 16 33 18',
                     fill: 'none', stroke: colour, 'stroke-width': 1.3, 'stroke-linecap': 'round' }, g);
      for (const x of [9, 13, 17, 21, 25, 29]) node('circle', { cx: x + 1, cy: 25.5, r: .9, fill: '#d9d9d9' }, g);
      break;
  }
}

/// Bildchen eines Slots. colour ist die Farbe der Kachel. Ohne role zeigt ein
/// NAM-Slot das allgemeine NAM-Bildchen.
export function slotArt (type, role, colour, size = 36) {
  const art = ART[type === 'NAM' && role ? `NAM:${role}` : type] || { shape: 'stomp', rows: [2] };
  const svg = node('svg', { class: 'rp-art', 'data-shape': art.shape, width: size, height: size,
                            viewBox: '0 0 40 40', 'aria-hidden': 'true' });
  draw(svg, art, colour || '#888888');
  return svg;
}
