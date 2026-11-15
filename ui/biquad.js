// Dieselben Formeln wie im Kern (core/src/dsp/Biquad.cpp).
//
// Die Bänder überlappen, die Kurve zeigt deshalb das Produkt aller Bänder. Im
// Programm fragt die UI den Kern (eq.response) und zeichnet dessen Antwort.
// Diese Datei ist der Weg für die Demo.

const TWO_PI = Math.PI * 2;

function normalise (b0, b1, b2, a0, a1, a2) {
  const inv = 1 / a0;
  return { b0: b0 * inv, b1: b1 * inv, b2: b2 * inv, a1: a1 * inv, a2: a2 * inv };
}

const clampFreq = (f, sr) => Math.min(sr * .49, Math.max(10, f));

export function designBell (freq, q, gainDb, sr) {
  const w0 = TWO_PI * clampFreq(freq, sr) / sr;
  const A = Math.pow(10, gainDb / 40);
  const alpha = Math.sin(w0) / (2 * Math.max(.05, q));
  const cosw = Math.cos(w0);
  return normalise(1 + alpha * A, -2 * cosw, 1 - alpha * A,
                   1 + alpha / A, -2 * cosw, 1 - alpha / A);
}

export function designLowShelf (freq, q, gainDb, sr) {
  const w0 = TWO_PI * clampFreq(freq, sr) / sr;
  const A = Math.pow(10, gainDb / 40);
  const cosw = Math.cos(w0), sinw = Math.sin(w0);
  const alpha = sinw / (2 * Math.max(.05, q));
  const tsa = 2 * Math.sqrt(A) * alpha;
  return normalise(
    A * ((A + 1) - (A - 1) * cosw + tsa),
    2 * A * ((A - 1) - (A + 1) * cosw),
    A * ((A + 1) - (A - 1) * cosw - tsa),
    (A + 1) + (A - 1) * cosw + tsa,
    -2 * ((A - 1) + (A + 1) * cosw),
    (A + 1) + (A - 1) * cosw - tsa);
}

export function designHighShelf (freq, q, gainDb, sr) {
  const w0 = TWO_PI * clampFreq(freq, sr) / sr;
  const A = Math.pow(10, gainDb / 40);
  const cosw = Math.cos(w0), sinw = Math.sin(w0);
  const alpha = sinw / (2 * Math.max(.05, q));
  const tsa = 2 * Math.sqrt(A) * alpha;
  return normalise(
    A * ((A + 1) + (A - 1) * cosw + tsa),
    -2 * A * ((A - 1) + (A + 1) * cosw),
    A * ((A + 1) + (A - 1) * cosw - tsa),
    (A + 1) - (A - 1) * cosw + tsa,
    2 * ((A - 1) - (A + 1) * cosw),
    (A + 1) - (A - 1) * cosw - tsa);
}

export function designHighPass (freq, q, sr) {
  const w0 = TWO_PI * clampFreq(freq, sr) / sr;
  const cosw = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * Math.max(.05, q));
  return normalise((1 + cosw) / 2, -(1 + cosw), (1 + cosw) / 2,
                   1 + alpha, -2 * cosw, 1 - alpha);
}

export function designLowPass (freq, q, sr) {
  const w0 = TWO_PI * clampFreq(freq, sr) / sr;
  const cosw = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * Math.max(.05, q));
  return normalise((1 - cosw) / 2, 1 - cosw, (1 - cosw) / 2,
                   1 + alpha, -2 * cosw, 1 - alpha);
}

/// FilterShape: 0 bell · 1 lowShelf · 2 highShelf · 3 lowCut · 4 highCut
export function designShape (shape, freq, q, gainDb, sr) {
  switch (shape) {
    case 1: return designLowShelf(freq, q, gainDb, sr);
    case 2: return designHighShelf(freq, q, gainDb, sr);
    case 3: return designHighPass(freq, q, sr);
    case 4: return designLowPass(freq, q, sr);
    default: return designBell(freq, q, gainDb, sr);
  }
}

export function magnitudeAt (c, hz, sr) {
  const w = TWO_PI * hz / sr;
  const cw = Math.cos(-w), sw = Math.sin(-w);
  const c2w = Math.cos(-2 * w), s2w = Math.sin(-2 * w);

  const nr = c.b0 + c.b1 * cw + c.b2 * c2w;
  const ni = c.b1 * sw + c.b2 * s2w;
  const dr = 1 + c.a1 * cw + c.a2 * c2w;
  const di = c.a1 * sw + c.a2 * s2w;

  return Math.hypot(nr, ni) / Math.max(1e-12, Math.hypot(dr, di));
}

/// Das Produkt, nicht die Summe der Punkte.
export function curveDb (bands, hz, sr) {
  let mag = 1;
  for (const b of bands)
    mag *= magnitudeAt(designShape(b.shape, b.freq, b.q, b.gain, sr), hz, sr);
  return 20 * Math.log10(Math.max(1e-9, mag));
}
