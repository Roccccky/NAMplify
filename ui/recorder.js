// Recorder-Karte.

import { el, makeFootswitch, pegelSkala } from './controls.js';
import { icon } from './icons.js';
import { call, tryAsk } from './bridge.js';

// Rot nur bei Aufnahme, beim Abspielen weiß. LED, Scheibe, Wellenband und
// Zeiger nehmen die Farben von hier.
const REC_COLOUR  = '#e2574c';
const PLAY_COLOUR = '#f2efe9';

export function buildRecorderCard (body, card, colour) {
  // Aufgebaut wie eine liegende Looper-Spur: vorn der Taster, dahinter die
  // Bahn, die von links nach rechts aufnimmt, am Ende die beiden Pegel.
  const wrap = el('div', 'recorder-wrap', body);
  const footHost = el('div', 'recorder-foot', wrap);

  const waveBox = el('div', 'recorder-wave wave-well', wrap);
  const canvas = el('canvas', '', waveBox);

  // Speichern über der Welle: liegt ein Take, erscheint beim Überfahren ein
  // Speicherzeichen über der ganzen Welle, die Welle wird dabei grau. Während
  // der Aufnahme erscheint es nicht.
  const save = el('button', 'recorder-save', waveBox);
  save.appendChild(icon('save'));
  save.title = 'Save this take';
  save.addEventListener('click', (e) => { e.stopPropagation(); call('recorder.save'); });

  const meters = el('div', 'recorder-meters', wrap);
  const meterL = el('div', 'recorder-meter', meters);
  const meterR = el('div', 'recorder-meter', meters);
  const fillL = el('div', 'recorder-meter-fill', meterL);
  const fillR = el('div', 'recorder-meter-fill', meterR);

  let peaks = null, state = 0, pos = 0, seconds = 0;
  // Farbe der Welle (siehe unten).
  let wave = colour;
  // Farbe des aktuellen Zustands: Aufnahme rot, Wiedergabe weiß, sonst die
  // Kartenfarbe.
  let running = colour;

  const buildSwitch = (row) => {
    // Der Fußschalter zeigt, was als Nächstes passiert: leeres Band roter
    // Aufnahmepunkt, vorhandener Take weißes Play-Dreieck.
    const fs = makeFootswitch({
      colour,
      onTap: () => call('recorder.tap'),
      onHold: () => call('recorder.erase'),
      title: 'Tap: record · Hold: erase',
    });
    card.recFoot = fs;
    row.appendChild(fs);
  };
  buildSwitch(footHost);

  card.meters = (m) => {
    const r = m?.recorder;
    if (!r) return;

    state = r.state; pos = r.pos; seconds = r.seconds;

    // In dB wie die Pegel oben: 0 dB bei drei Vierteln der Bahn.
    fillL.style.setProperty('--v', pegelSkala(r.l));
    fillR.style.setProperty('--v', pegelSkala(r.r));

    // Während der Aufnahme kein Speicherzeichen. Sonst entscheidet das
    // Stylesheet beim Überfahren.
    waveBox.dataset.saveable = (r.has && state !== 1) ? 'true' : 'false';

    // Die Welle bleibt in der Kartenfarbe, nur LED und Taster werden weiß. Die
    // Welle zeigt, was aufgenommen ist, den Zustand zeigen LED und Scheibe.
    wave = state === 1 ? REC_COLOUR : colour;
    running = state === 1 ? REC_COLOUR : state === 2 ? PLAY_COLOUR : colour;
    waveBox.style.setProperty('--track-colour', wave);
    waveBox.dataset.state = state === 1 ? 'recording'
                          : state === 2 ? 'playing'
                          : state === 3 ? 'stopped' : 'empty';

    if (card.recFoot) {
      card.recFoot.setOn(state === 1 || state === 2);
      // Die ganze Scheibe trägt die Farbe, damit man den Zustand auch aus
      // Abstand sieht.
      card.recFoot.style.setProperty('--knob-colour', running);
      card.recFoot.title = state === 0 ? 'Tap: record · Hold: erase'
                        : state === 1 ? 'Tap: stop · Hold: erase'
                        : state === 3 ? 'Tap: play · Hold: erase'
                                      : 'Tap: stop · Hold: erase';
      const led = card.recFoot.querySelector('.footswitch-led');
      if (led) {
        // Nur der Taster zeigt den Zustand, die Looper-Spuren tragen kein
        // Zeichen. Vier Zustände, zwei Zeichen: Punkt heißt Aufnahme, Dreieck
        // Wiedergabe. Leer und gestoppt zeigen, was der nächste Tritt tut,
        // aufnehmend und laufend, was gerade passiert.
        led.dataset.sign = (state === 0 || state === 1) ? 'record' : 'play';

        // Auf heller Scheibe wird das Zeichen dunkel. Bei der Wiedergabe ist
        // die Scheibe fast weiß.
        const an = state === 1 || state === 2;
        led.style.background = an ? 'var(--switch-bot)'
                             : state === 0 ? REC_COLOUR : PLAY_COLOUR;
      }
    }

    // Der Statusstreifen pulsiert langsam, solange die Maschine läuft. Rot nur
    // bei Aufnahme, beim Abspielen weiß. Kein hartes Blinken.
    if (state === 1) {
      card.stripeFill.style.background = REC_COLOUR;
      card.stripeFill.dataset.breathe = 'true';
      card.stripeFill.style.opacity = '1';
    } else if (state === 2) {
      card.stripeFill.style.background = PLAY_COLOUR;
      card.stripeFill.dataset.breathe = 'true';
      card.stripeFill.style.opacity = '1';
    } else {
      card.stripeFill.dataset.breathe = 'false';
      card.stripeFill.style.background = colour;
      card.stripeFill.style.opacity = r.has ? '1' : '0';
    }

    draw();
  };

  card.update = async () => {
    const [data] = await tryAsk('recorder.waveform', { points: 320 });
    if (data) { peaks = data.peaks; seconds = data.seconds; draw(); }
  };
  card.update();

  // Während der Aufnahme wächst die Welle mit, deshalb regelmäßig nachladen.
  setInterval(() => { if (state === 1) card.update(); }, 700);

  function draw () {
    const rect = waveBox.getBoundingClientRect();
    if (!rect.width || !rect.height) return;

    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(rect.width * dpr);
    canvas.height = Math.round(rect.height * dpr);

    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, rect.width, rect.height);

    if (!peaks) return;

    // Liegende Welle wie beim Looper: links früher, rechts später, Ausschläge
    // aus der Mitte.
    const W = rect.width, H = rect.height, mid = H / 2;
    const xOf = (u) => u * W;

    ctx.lineWidth = 1;
    for (let i = 0; i < peaks.length; ++i) {
      const u = i / (peaks.length - 1);
      const x = xOf(u);
      const a = peaks[i] * mid * .92;

      // Der Zeiger läuft umgekehrt zum Looper: was noch kommt, ist blass, das
      // Abgespielte voll.
      const played = u <= pos;
      ctx.strokeStyle = played ? wave : 'rgba(255,255,255,.18)';
      ctx.globalAlpha = played ? .9 : .55;
      ctx.beginPath();
      ctx.moveTo(x, mid - a);
      ctx.lineTo(x, mid + a);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    if (state !== 0) {
      const x = xOf(pos);
      ctx.strokeStyle = state === 1 ? REC_COLOUR : 'rgba(255,255,255,.6)';
      ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
    }

    // Keine Zeitanzeige auf dem Band.
  }
}
