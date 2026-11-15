// Looper-Karte.

import { el, makeKnob, makeFootswitch } from './controls.js';
import { store, notify } from './store.js';
import { call, ask } from './bridge.js';
import { showToast } from './dialogs.js';

/// Vier Spurfarben: Bernstein, Grün, Violett, Blau. Gut unterscheidbar, auch
/// klein. Kein Rot, das steht für Aufnahme und Amp.
///
/// Die Karte selbst ist grau, welche Spur gerade gilt, zeigt der
/// Leuchtstreifen.
const TRACK_COLOURS = ['#c9a227', '#4caf6d', '#9b6cd6', '#5a8fd8'];

/// Der Tooltip nennt immer auch das Löschen (langer Druck).
const TIPS = {
  EMPTY:     'Tap: record   ·   Hold: erase',
  RECORDING: 'Tap: close the loop   ·   Hold: erase',
  PLAYING:   'Tap: stop   ·   Hold: erase',
  STOPPED:   'Tap: play   ·   Hold: erase',
};

export function buildLooperCard (body, card, colour) {
  const wrap = el('div', 'looper-tracks', body);
  const tracks = [];

  // Liegend, eine Spur je Zeile: vorn der Taster, dann der Regler, dann die
  // Bahn, die von links nach rechts aufnimmt.
  for (let i = 0; i < 4; ++i) {
    const trackColour = TRACK_COLOURS[i];
    const node = el('div', 'looper-track', wrap);
    node.style.setProperty('--track-colour', trackColour);

    const foot = makeFootswitch({
      colour: trackColour, small: true,
      onTap: () => call('looper.tap', { track: i }),
      onHold: () => call('looper.erase', { track: i }),
      title: TIPS.EMPTY,
    });
    node.appendChild(foot);

    // Je Spur ein kleiner Lautstärkeregler.
    const vol = makeVolumeKnob(i, trackColour);
    node.appendChild(vol.root);

    const wave = el('div', 'looper-wave wave-well', node);
    const canvas = el('canvas', '', wave);

    // Zwei Anfasser an der fertigen Schleife: links der Anfang, rechts das
    // Ende.
    const endHandle = el('div', 'looper-handle', wave);
    endHandle.dataset.edge = 'end';
    const startHandle = el('div', 'looper-handle', wave);
    startHandle.dataset.edge = 'start';

    // Das Längenschild hängt an der Spurenreihe, nicht an der Spur. In der Spur
    // würde es abgeschnitten (overflow: hidden hält dort die Welle im Rahmen).
    const lengthTag = el('div', 'looper-length', wrap);
    lengthTag.hidden = true;

    const track = { index: i, colour: trackColour, node, wave, canvas, vol, foot,
                    endHandle, startHandle, lengthTag,
                    peaks: null, info: null, zoomEdge: null, dragEdge: null };

    setupEdgeDrag(track, 'start', startHandle);
    setupEdgeDrag(track, 'end', endHandle);

    // Tippen und langer Druck gelten für die ganze Spur, nicht nur für den
    // Taster.
    setupTapAndHold(wave, i);

    tracks.push(track);
  }

  card.looperTracks = tracks;


  card.meters = (m) => {
    if (!m?.looper) return;

    tracks.forEach((track, i) => {
      const info = m.looper[i];

      // Die Wellenform kommt nicht mit den Metern mit und wird separat geholt.
      const vorher = track.info;
      const schrieb = vorher && vorher.state === 'RECORDING';
      const schreibt = info.state === 'RECORDING';
      if ((vorher?.rawLength || 0) !== (info.rawLength || 0) || (schrieb && !schreibt))
        requestWave(track);

      // Während an einer Kante gezogen wird, behalten die Kanten ihren lokalen
      // Stand. Die Meldung kommt 30-mal je Sekunde und würde die Kante sonst
      // zurücksetzen.
      track.info = (track.dragEdge && vorher)
        ? { ...info, loopStart: vorher.loopStart, loopEnd: vorher.loopEnd }
        : info;
      // Der Zustand steht an der Bahn (Farbe, Pulsieren bei Aufnahme).
      // Kleingeschrieben für das Stylesheet.
      track.wave.dataset.state = String(info.state || 'EMPTY').toLowerCase();
      track.foot?.setOn(info.state !== 'EMPTY' && info.state !== 'STOPPED');
      if (track.foot) track.foot.title = TIPS[info.state] || TIPS.EMPTY;
      // Kein Zeichen auf den Spurtastern, dafür sind sie zu klein. Farbe und
      // gefüllt/Ring reichen.
      track.vol.setValue(info.vol);
      // Gezeichnet wird der lokale Stand der Spur, beim Ziehen weicht er von
      // der Meldung ab.
      drawTrack(track, track.info);
    });

    // Der Statusstreifen pulsiert, solange eine Spur aufnimmt. Läuft nur
    // Wiedergabe, leuchtet er ruhig, ist alles leer, ist er dunkel.
    const rec = m.looperRecording ?? -1;
    const old = m.looperOldest ?? -1;

    // Der Streifen ist weiß, nicht in der Spurfarbe. Die Spurfarbe steht schon
    // an Regler, Bahn, Rahmen und Taster.
    const LED = '#f2efe9';

    if (rec >= 0) {
      card.stripeFill.style.background = LED;
      card.stripeFill.dataset.breathe = 'true';
      card.stripeFill.style.opacity = '1';
    } else if (old >= 0) {
      card.stripeFill.style.background = LED;
      card.stripeFill.dataset.breathe = 'false';
      card.stripeFill.style.opacity = '1';
    } else {
      card.stripeFill.style.background = LED;
      card.stripeFill.dataset.breathe = 'false';
      card.stripeFill.style.opacity = '0';
    }
  };

  card.update = () => { for (const t of tracks) requestWave(t); };
  for (const t of tracks) requestWave(t);
}

// Lautstärke
function makeVolumeKnob (index, colour) {
  const root = el('div', 'knob');
  root.dataset.small = 'true';
  root.style.setProperty('--knob-colour', colour);
  root.style.setProperty('--knob', 'calc(26px * var(--scale))');

  const dial = el('div', 'knob-dial', root);
  dial.style.width = 'calc(26px * var(--scale))';
  dial.style.height = 'calc(26px * var(--scale))';

  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 100 100');
  const ring = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  ring.setAttribute('cx', 50); ring.setAttribute('cy', 50); ring.setAttribute('r', 40);
  // Neutrales Grau wie das übrige Blech.
  ring.setAttribute('fill', '#272727'); ring.setAttribute('stroke', '#0b0b0b');
  const arc = document.createElementNS('http://www.w3.org/2000/svg', 'path');
  arc.setAttribute('fill', 'none'); arc.setAttribute('stroke', colour);
  arc.setAttribute('stroke-width', '7'); arc.setAttribute('stroke-linecap', 'round');

  // Derselbe Zeiger wie an jedem anderen Regler, nur kleiner.
  const pointer = document.createElementNS('http://www.w3.org/2000/svg', 'line');
  pointer.setAttribute('class', 'knob-pointer');
  pointer.setAttribute('stroke-width', '6');

  svg.append(ring, arc, pointer);
  dial.appendChild(svg);

  let value = .8, dragging = false, startY = 0, startValue = 0;

  const draw = () => {
    const A0 = -225 * Math.PI / 180, A1 = 45 * Math.PI / 180;
    const a = A0 + (A1 - A0) * value;
    const R = 40;
    const x0 = 50 + R * Math.cos(A0), y0 = 50 + R * Math.sin(A0);
    const x1 = 50 + R * Math.cos(a), y1 = 50 + R * Math.sin(a);
    arc.setAttribute('d', value > .01
      ? `M ${x0} ${y0} A ${R} ${R} 0 ${Math.abs(a - A0) > Math.PI ? 1 : 0} 1 ${x1} ${y1}` : '');

    // Der Zeiger ist ein kurzer Strich am Rand wie am großen Regler.
    const c = Math.cos(a), sn = Math.sin(a);
    pointer.setAttribute('x1', 50 + 24 * c);
    pointer.setAttribute('y1', 50 + 24 * sn);
    pointer.setAttribute('x2', 50 + 40 * c);
    pointer.setAttribute('y2', 50 + 40 * sn);
  };

  dial.addEventListener('pointerdown', (e) => {
    e.preventDefault(); e.stopPropagation();
    dial.setPointerCapture(e.pointerId);
    dragging = true; startY = e.clientY; startValue = value;
  });
  dial.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    value = Math.min(1, Math.max(0, startValue + (startY - e.clientY) / 120));
    draw();
    call('looper.volume', { track: index, value });
  });
  const stop = (e) => { dragging = false; try { dial.releasePointerCapture(e.pointerId); } catch {} };
  dial.addEventListener('pointerup', stop);
  dial.addEventListener('pointercancel', stop);

  draw();

  return {
    root,
    setValue: (v) => {
      if (dragging) return;
      value = v; draw();
      // Auf einer leeren Spur gibt es nichts zu regeln, das soll man sehen.
      root.dataset.disabled = 'false';
    },
    setDisabled: (d) => { root.dataset.disabled = d ? 'true' : 'false'; },
  };
}

// Tippen/Halten
function setupTapAndHold (node, index) {
  let timer = null, held = false, moved = false, sx = 0, sy = 0;

  node.addEventListener('pointerdown', (e) => {
    if (e.target.closest('.looper-handle')) return;
    e.stopPropagation();
    held = false; moved = false;
    sx = e.clientX; sy = e.clientY;
    timer = setTimeout(() => { held = true; call('looper.erase', { track: index }); }, 550);
  });

  node.addEventListener('pointermove', (e) => {
    if (Math.hypot(e.clientX - sx, e.clientY - sy) > 8) { moved = true; clearTimeout(timer); }
  });

  node.addEventListener('pointerup', (e) => {
    e.stopPropagation();
    clearTimeout(timer);
    if (!held && !moved) call('looper.tap', { track: index });
    held = false;
  });

  node.addEventListener('pointercancel', () => { clearTimeout(timer); held = false; });
}

// Kanten. Beim Greifen zoomt die Anzeige an die Kante, die Spur zeigt dann nur
// ein Fenster um sie. Das Fenster wird beim Greifen einmal gesetzt und bleibt,
// sonst verschiebt sich beim Ziehen der Maßstab.
const ZOOM_SPAN = .28;          // Anteil der Rohlänge, der beim Ziehen zu sehen ist

function zoomWindow (track, edge) {
  const info = track.info;
  if (!info?.rawLength) return null;
  const at = (edge === 'start' ? info.loopStart : info.loopEnd) / info.rawLength;
  let lo = at - ZOOM_SPAN / 2, hi = at + ZOOM_SPAN / 2;
  if (lo < 0) { hi -= lo; lo = 0; }
  if (hi > 1) { lo -= hi - 1; hi = 1; }
  return { lo: Math.max(0, lo), hi: Math.min(1, hi) };
}

const viewOf = (track) => track.view || { lo: 0, hi: 1 };

function setupEdgeDrag (track, edge, handle) {
  let dragging = false;

  handle.addEventListener('pointerdown', (e) => {
    if (!track.info || !track.info.rawLength) return;
    e.preventDefault(); e.stopPropagation();
    handle.setPointerCapture(e.pointerId);
    dragging = true;
    // Beim Greifen an die Kante zoomen.
    track.zoomEdge = edge;
    track.dragEdge = edge;
    track.view = zoomWindow(track, edge);
    track.lengthTag.hidden = false;
    drawTrack(track, track.info);
  });

  handle.addEventListener('pointermove', (e) => {
    if (!dragging || !track.info) return;

    const r = track.wave.getBoundingClientRect();
    // Links früher, rechts später, gerechnet im beim Greifen gesetzten Fenster.
    const { lo, hi } = viewOf(track);
    const unit = lo + ((e.clientX - r.left) / r.width) * (hi - lo);
    const raw = track.info.rawLength;
    let pos = Math.round(Math.min(1, Math.max(0, unit)) * raw);

    const info = track.info;
    if (edge === 'start') pos = Math.min(pos, info.loopEnd - 2400);
    else                  pos = Math.max(pos, info.loopStart + 2400);

    // Gemessen wird von der Kante, die stehen bleibt.
    const fixed = edge === 'start' ? info.loopEnd : info.loopStart;
    const length = Math.abs(pos - fixed);
    updateLengthTag(track, length, e.clientX - r.left);

    const start = edge === 'start' ? pos : info.loopStart;
    const end   = edge === 'end'   ? pos : info.loopEnd;
    track.info = { ...info, loopStart: start, loopEnd: end };
    drawTrack(track, track.info);
  });

  const stop = (e) => {
    if (!dragging) return;
    dragging = false;
    try { handle.releasePointerCapture(e.pointerId); } catch {}
    track.zoomEdge = null;
    track.dragEdge = null;
    track.view = null;
    track.lengthTag.hidden = true;
    call('looper.edges', { track: track.index,
                           start: track.info.loopStart, end: track.info.loopEnd })
      .then(() => requestWave(track));
  };

  handle.addEventListener('pointerup', stop);
  handle.addEventListener('pointercancel', stop);
}

/// An der gezogenen Kante steht die Länge im Klartext (2 BARS, 6 BEATS, 5.5
/// BEATS). Bei ganzen Takten wird die Anzeige kräftig.
function updateLengthTag (track, lengthSamples, x) {
  const cycle = track.cycle || 0;
  const tag = track.lengthTag;

  if (!cycle) {
    tag.textContent = `${(lengthSamples / (track.sampleRate || 48000)).toFixed(2)} s`;
    tag.dataset.whole = 'false';
    return;
  }

  const bars = lengthSamples / cycle;
  const beats = bars * 4;
  const wholeBars = Math.abs(bars - Math.round(bars)) < 0.03 && Math.round(bars) >= 1;

  // Nie 2.0 BEATS, das wäre nur Rundungsrest.
  tag.textContent = wholeBars
    ? `${Math.round(bars)} BAR${Math.round(bars) === 1 ? '' : 'S'}`
    : `${beats.toFixed(1).replace(/\.0$/, '')} BEATS`;
  tag.dataset.whole = wholeBars ? 'true' : 'false';
  stellSchild(track, x);
}

/// Das Schild steht über der Kante, oben in der gezogenen Spur, und bleibt ganz
/// im Bild.
function stellSchild (track, x) {
  const tag = track.lengthTag;
  const parent = tag.offsetParent;
  if (!parent) return;
  const wr = track.wave.getBoundingClientRect();
  const pr = parent.getBoundingClientRect();
  const links = wr.left - pr.left + x - tag.offsetWidth / 2;
  tag.style.left = `${Math.round(Math.max(0, Math.min(pr.width - tag.offsetWidth, links)))}px`;
  tag.style.top = `${Math.round(Math.max(0, wr.top - pr.top + 2))}px`;
}

// Zeichnen
async function requestWave (track) {
  const [data] = await import('./bridge.js').then((m) =>
    m.tryAsk('looper.waveform', { track: track.index, points: 256 }));
  if (!data) return;
  track.peaks = data.peaks;
  track.cycle = data.cycle;
  track.sampleRate = data.sampleRate;
  track.info = { ...(track.info || {}), rawLength: data.rawLength,
                 loopStart: data.loopStart, loopEnd: data.loopEnd };
  drawTrack(track, track.info);
}

function drawTrack (track, info) {
  const canvas = track.canvas;
  const rect = track.wave.getBoundingClientRect();
  if (!rect.width || !rect.height) return;

  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(rect.width * dpr);
  canvas.height = Math.round(rect.height * dpr);

  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, rect.width, rect.height);

  // Anfasser nur an einer fertigen Schleife. Das steht vor dem Ausstieg, damit
  // sie auf der leeren Spur verschwinden.
  track.startHandle.hidden = !info?.rawLength;
  track.endHandle.hidden = !info?.rawLength;

  const peaks = track.peaks;
  if (!peaks || !info?.rawLength) return;

  const W = rect.width, H = rect.height, mid = H / 2;
  const raw = info.rawLength;
  const start = info.loopStart / raw, end = info.loopEnd / raw;

  // Alles läuft durch dasselbe Fenster (Zoom an der gegriffenen Kante). Ohne
  // Ziehen ist es die ganze Spur.
  const { lo, hi } = viewOf(track);
  const span = (hi - lo) || 1;
  const xOf = (u) => ((u - lo) / span) * W;

  // Rasterlinien beim Ziehen: Takt kräftig, Beat mittel, Zwischenstufen
  // schwach.
  if (track.dragEdge && track.cycle) {
    const fixed = track.dragEdge === 'start' ? info.loopEnd : info.loopStart;
    // In Sechzehnteln gezählt, sonst gäbe es keine Zwischenstufen.
    const schritt = track.cycle / 16;
    const kMax = Math.min(512, Math.ceil(raw / schritt));
    for (let k = -kMax; k <= kMax; ++k) {
      const pos = fixed + k * schritt;
      if (pos < 0 || pos > raw) continue;
      const x = xOf(pos / raw);
      if (x < -1 || x > W + 1) continue;
      const isBar = k % 16 === 0;
      const isBeat = k % 4 === 0;
      ctx.strokeStyle = isBar ? 'rgba(255,255,255,.30)'
                    : isBeat ? 'rgba(255,255,255,.14)' : 'rgba(255,255,255,.05)';
      ctx.lineWidth = isBar ? 1.2 : 1;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
    }
  }

  // Liegende Welle: links früher, rechts später, Ausschläge aus der Mitte.
  for (let i = 0; i < peaks.length; ++i) {
    const u = i / (peaks.length - 1);
    const x = xOf(u);
    if (x < 0 || x > W) continue;
    const a = peaks[i] * mid * .92;

    // Was außerhalb der Schleife liegt, bleibt blass sichtbar und kann
    // zurückgeholt werden.
    const inside = u >= start && u <= end;
    ctx.strokeStyle = inside ? track.colour : 'rgba(255,255,255,.14)';
    ctx.globalAlpha = inside ? .85 : .5;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x, mid - a);
    ctx.lineTo(x, mid + a);
    ctx.stroke();
  }
  ctx.globalAlpha = 1;

  // Ein Schatten läuft über das schon Gespielte. Das Feld heißt pos (so sendet
  // es plugin/src/ui/WebBridge.cpp).
  if (info.state !== 'EMPTY' && info.pos !== undefined) {
    const px = Math.max(0, Math.min(W, xOf(start + (end - start) * info.pos)));
    ctx.fillStyle = 'rgba(0,0,0,.42)';
    ctx.fillRect(0, 0, px, H);
    ctx.strokeStyle = 'rgba(255,255,255,.5)';
    ctx.beginPath(); ctx.moveTo(px, 0); ctx.lineTo(px, H); ctx.stroke();
  }

  // Kanten außerhalb des Fensters: der Anfasser bleibt am Rand stehen.
  const klemm = (v) => Math.max(0, Math.min(100, v));
  track.startHandle.style.left = `${klemm(((start - lo) / span) * 100)}%`;
  track.endHandle.style.right = `${klemm((1 - (end - lo) / span) * 100)}%`;
}
