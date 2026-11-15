// Alle Symbole der UI als Pfade, ohne Icon-Font und ohne Bilddateien, damit sie
// sich einfärben lassen und bei jeder Größe scharf bleiben. Alle Pfade liegen
// in einem 24×24-Feld.

const P = {
  // Kopfleiste
  tuningFork: 'M9 3v7a3 3 0 0 0 3 3v0a3 3 0 0 0 3-3V3 M12 13v8',
  gear: 'M12 15.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7Z '
      + 'M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 '
      + '1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.6 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.6a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1Z',
  reset: 'M3 12a9 9 0 1 0 3-6.7 M3 4v5h5',

  // Pfeile
  chevronUp:    'M6 15l6-6 6 6',
  chevronDown:  'M6 9l6 6 6-6',
  chevronLeft:  'M15 6l-6 6 6 6',
  chevronRight: 'M9 6l6 6-6 6',
  arrowDown:    'M12 4v14 M6 13l6 6 6-6',
  arrowUp:      'M12 20V6 M6 11l6-6 6 6',
  triangleLeft:  'M15 5l-8 7 8 7z',
  triangleRight: 'M9 5l8 7-8 7z',
  play: 'M8 5l11 7-11 7z',

  // Aktionen
  plus:  'M12 5v14 M5 12h14',
  close: 'M6 6l12 12 M18 6L6 18',
  check: 'M4 13l5 5L20 6',
  trash: 'M4 7h16 M10 11v6 M14 11v6 M6 7l1 13h10l1-13 M9 7V4h6v3',
  download: 'M12 4v11 M7 11l5 5 5-5 M4 20h16',
  drive: 'M4 6h16v9H4z M6 18h12 M7 9h6 M17 9h.01',
  save: 'M5 4h11l3 3v13H5z M8 4v6h7V4 M8 20v-6h8v6',
  wand: 'M4 20L15 9 M13 7l4 4 M19 3v4 M17 5h4 M8 4v2 M7 5h2 M19 14v2 M18 15h2',
  folder: 'M4 6h6l2 2h8v11H4z',
  list: 'M4 6h16 M4 12h16 M4 18h16',
  grid: 'M4 4h7v7H4z M13 4h7v7h-7z M4 13h7v7H4z M13 13h7v7h-7z',
  spinner: 'M12 3a9 9 0 1 0 9 9',
  link: 'M10 13a5 5 0 0 0 7 0l3-3a5 5 0 0 0-7-7l-1 1 M14 11a5 5 0 0 0-7 0l-3 3a5 5 0 0 0 7 7l1-1',

  // Einstellungen
  speaker: 'M5 9h4l5-4v14l-5-4H5z M17 9a4 4 0 0 1 0 6',
  cloud:   'M7 18a4 4 0 0 1 .8-7.9 5.5 5.5 0 0 1 10.6 1.4A3.5 3.5 0 0 1 17.5 18z',
  sliders: 'M4 8h10 M18 8h2 M4 16h4 M12 16h8 M15 5v6 M9 13v6',
  /* Global-EQ: drei Schieber mit Kappen. Das Kartenzeichen der EQ-Karte steht
     in CARD_MARK. */
  eq:      'M6 4v5 M6 15v5 M12 4v3 M12 13v7 M18 4v7 M18 17v3 '
         + 'M4 12h4 M10 10h4 M16 14h4',
  midi:    'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18z M8 15V9h1.5l2.5 3 2.5-3H16v6',
  books:   'M5 4h4v16H5z M11 4h4v16h-4z M17 5l3 15',
  search:  'M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14z M16 16l4.5 4.5',
  heart:   'M12 20s-7-4.4-7-10a4 4 0 0 1 7-2.6A4 4 0 0 1 19 10c0 5.6-7 10-7 10z',

};

const VIEWBOX = '0 0 24 24';

/// Symbol als SVG-Element.
export function icon (name, { size = null, fill = false } = {}) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', VIEWBOX);
  svg.setAttribute('fill', 'none');
  svg.setAttribute('stroke', fill ? 'none' : 'currentColor');
  svg.setAttribute('stroke-width', '1.6');
  svg.setAttribute('stroke-linecap', 'round');
  svg.setAttribute('stroke-linejoin', 'round');
  if (size) { svg.setAttribute('width', size); svg.setAttribute('height', size); }

  const d = P[name] || P.close;
  const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
  path.setAttribute('d', d);
  if (fill) path.setAttribute('fill', 'currentColor');
  svg.appendChild(path);
  return svg;
}

/// Kartenzeichen. Als Markup statt als einzelner Pfad, weil sie aus mehreren
/// Formen bestehen. Auch sie liegen in einem 24×24-Feld und nutzen
/// currentColor.
const CARD_MARK = {
  // Kartenzeichen: ein Strichgewicht, runde Enden, alles im 24×24-Feld,
  // angelehnt an die macOS-Systemsymbole, aber selbst gezeichnet. Gezeigt wird
  // das Gerät, nicht seine Wirkung. Nur wo es kein Gerät gibt (Hall, Delay,
  // Chorus), stehen Wellen.

  // Gate: das Signal wird an einer Linie abgeschnitten.
  GATE: '<path d="M3 12h4l2.5-6 3 12 2.5-6h1"/>'
      + '<path d="M19 4v16"/>',

  // Kompressor: zwei Pfeile drücken auf eine Linie.
  COMP: '<path d="M4 12h16"/>'
      + '<path d="M12 3v5M9.5 5.5 12 3l2.5 2.5"/>'
      + '<path d="M12 21v-5M9.5 18.5 12 21l2.5-2.5"/>',

  // Zerre: der Blitz.
  DRIVE: '<path d="M13.5 2 5 13.5h5.5L9.5 22 19 10.5h-5.5L14.5 2z" '
       + 'fill="currentColor" stroke="none"/>',

  // Pedal: ein Chip mit Beinchen.
  PEDAL: '<rect x="7.5" y="7.5" width="9" height="9" rx="1.6"/>'
       + '<path d="M10 3.5v4M14 3.5v4M10 16.5v4M14 16.5v4'
       + 'M3.5 10h4M3.5 14h4M16.5 10h4M16.5 14h4"/>',

  // Amp: ein Verstärkerkopf mit Griff.
  AMP: '<path d="M9.5 5.5V5a1.5 1.5 0 0 1 1.5-1.5h2A1.5 1.5 0 0 1 14.5 5v.5"/>'
     + '<rect x="3" y="5.5" width="18" height="13" rx="2"/>'
     + '<path d="M6.5 15h11"/>'
     + '<circle cx="8" cy="10.5" r="1.4"/><circle cx="12" cy="10.5" r="1.4"/>'
     + '<circle cx="16" cy="10.5" r="1.4"/>',

  // Klangregler: drei stehende Schieber.
  EQ: '<path d="M7 3.5v17M12 3.5v17M17 3.5v17"/>'
    + '<path d="M4.5 9h5M9.5 15h5M14.5 7h5"/>',

  // Box: ein Kasten mit Lautsprecher.
  CAB: '<rect x="4.5" y="2.5" width="15" height="19" rx="2"/>'
     + '<circle cx="12" cy="14" r="4"/><circle cx="12" cy="14" r="1.3"/>'
     + '<circle cx="12" cy="6.5" r="1.3"/>',

  // Rackgerät: derselbe Chip, gefüllt.
  OUTBOARD: '<rect x="7.5" y="7.5" width="9" height="9" rx="1.6" '
          + 'fill="currentColor" stroke="none"/>'
          + '<path d="M10 3.5v4M14 3.5v4M10 16.5v4M14 16.5v4'
          + 'M3.5 10h4M3.5 14h4M16.5 10h4M16.5 14h4"/>',

  // Chorus: Linien, die auslaufen und sich einrollen.
  CHORUS: '<path d="M3 8h9a2.5 2.5 0 1 0-2.5-2.5"/>'
        + '<path d="M3 12h13a2.5 2.5 0 1 1-2.5 2.5"/>'
        + '<path d="M3 16h7a2 2 0 1 1-2 2"/>',

  // Flanger: eine Welle, die sich staucht.
  FLANGER: '<path d="M2.5 12c2-7 4.5 7 7 0s4.5-7 7 0 3 5 4 1.5"/>'
         + '<path d="M2.5 18c2-4 4.5 4 7 0s4.5-4 7 0 3 3 4 1"/>',

  // Phaser: der Kamm in der Kurve.
  PHASER: '<path d="M2.5 12h2.5l2-5.5 3 11 3-13 3 10 2-4.5h3.5"/>',

  // Tremolo: drei laufende Wellen.
  TREMOLO: '<path d="M2.5 7.5c2-2.5 3.5-2.5 5.5 0s3.5 2.5 5.5 0 3.5-2.5 5.5 0"/>'
         + '<path d="M2.5 12c2-2.5 3.5-2.5 5.5 0s3.5 2.5 5.5 0 3.5-2.5 5.5 0"/>'
         + '<path d="M2.5 16.5c2-2.5 3.5-2.5 5.5 0s3.5 2.5 5.5 0 3.5-2.5 5.5 0"/>',

  // Delay: ein Anschlag, und was ihm nachläuft.
  DELAY: '<circle cx="4.5" cy="12" r="1.6" fill="currentColor" stroke="none"/>'
       + '<path d="M9 8.5a5 5 0 0 1 0 7M13.5 6a8.5 8.5 0 0 1 0 12M18 3.5a12 12 0 0 1 0 17"/>',

  // Hall: derselbe Anschlag, aber er kommt von allen Seiten zurück.
  REVERB: '<circle cx="12" cy="12" r="1.6" fill="currentColor" stroke="none"/>'
        + '<path d="M8 9a4.5 4.5 0 0 0 0 6M16 9a4.5 4.5 0 0 1 0 6"/>'
        + '<path d="M4.5 6a8.5 8.5 0 0 0 0 12M19.5 6a8.5 8.5 0 0 1 0 12"/>',

  // Looper: es geht von vorn los.
  LOOPER: '<path d="M4 9.5V8a2.5 2.5 0 0 1 2.5-2.5h11"/>'
        + '<path d="M15 3l2.5 2.5L15 8"/>'
        + '<path d="M20 14.5V16a2.5 2.5 0 0 1-2.5 2.5h-11"/>'
        + '<path d="M9 16l-2.5 2.5L9 21"/>',

  // Drums: Metronom mit Pendel und Gewicht.
  DRUMS: '<path d="M9.5 3.5h5l4 17h-13z"/>'
       + '<path d="M7.5 15h9"/>'
       + '<path d="M12 18.5 16.5 6"/>',

  // Bandmaschine: die Spule mit dem Aufnahmepunkt.
  RECORDER: '<rect x="2.5" y="5.5" width="19" height="13" rx="2"/>'
          + '<circle cx="8" cy="12" r="2.6"/><circle cx="16" cy="12" r="2.6"/>'
          + '<path d="M10.6 12h2.8"/>',
};

export function cardIcon (cardId, { size = null } = {}) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', VIEWBOX);
  svg.setAttribute('fill', 'none');
  svg.setAttribute('stroke', 'currentColor');
  svg.setAttribute('stroke-width', '1.8');
  svg.setAttribute('stroke-linecap', 'round');
  svg.setAttribute('stroke-linejoin', 'round');
  if (size) { svg.setAttribute('width', size); svg.setAttribute('height', size); }
  svg.innerHTML = CARD_MARK[cardId] || CARD_MARK.AMP;
  return svg;
}

/// Kategoriezeichen für die Schublade, solange ein Bild fehlt.
export function categoryIcon (card, opts) {
  return cardIcon(card === 'CAB' ? 'CAB' : card, opts);
}

/// Symbol eines Slots: ein NAM-Slot trägt das seiner Rolle, eine IR das der
/// Box, die übrigen das ihres Typs.
export function slotIcon (type, role, opts) {
  if (type === 'NAM')
    return cardIcon({ amp: 'AMP', pedal: 'PEDAL', outboard: 'OUTBOARD', fullrig: 'AMP', space: 'REVERB' }[role] || 'AMP', opts);
  if (type === 'IR') return cardIcon('CAB', opts);
  return cardIcon(type, opts);
}
