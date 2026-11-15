# NAMplify auspacken

`NAMplify Auspacken.app` kopiert die Bibliothek in normale Ordner, für
Programme, die mit `.namplify`-Bündeln nichts anfangen können.

* Doppelklick: packt `Dokumente/NAMplify` aus.
* Ordner darauf ziehen: packt diesen Ordner aus.

Das Ergebnis landet auf dem Schreibtisch, mit Datum im Namen, ein zweiter Lauf
überschreibt den ersten also nicht. Die Bibliothek selbst bleibt unverändert,
es wird nur kopiert.

Ergebnis sind fünf flache Ordner nach Geräteart:

```
Fullrig/    Amp und Box in einem Capture
Amp/        nur der Amp
IR/         Impulsantworten
Pedals/     Pedale
Outboard/   Rackgeräte
```

Keine Unterordner, die Dateinamen enthalten Bank und Variante schon. Gleiche
Namen aus verschiedenen Bänken werden durchnummeriert, nie überschrieben.

Die Geräteart kommt aus der Metadatei (`*.t3k.json`), weil `NAM/Models` Amps
und Fullrigs enthält. Ohne Metadatei entscheidet der Ordner.

| | |
|---|---|
| `.nam`, `.wav` | werden kopiert |
| `*.t3k.json`, `*.jpg`, `*.png` | werden nicht kopiert (Metadaten und Vorschaubilder) |
| Aufnahmen (`Recordings/`) | werden nicht kopiert |

Ohne App:

```bash
tools/extract-library.sh [Quelle] [Ziel]
```

Das Skript liegt in der App (`Contents/Resources/extract-library.sh`), sie
läuft also auch ohne dieses Projekt.

Neu bauen, wenn sich das Skript ändert:

```bash
tools/make-extractor-app.sh [Zielordner]
```

`tools/build-and-test.sh all` legt die App außerdem neben die Artefakte
(`build/plugin/NAMplify_artefacts/Release/`), sie gehört zur Auslieferung.
