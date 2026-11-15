#!/usr/bin/env bash
# NAMplify-Bibliothek in normale Ordner auspacken.
#
#   tools/extract-library.sh [Quelle] [Ziel]
#
# Ergebnis sind fünf flache Ordner nach Geräteart:
#
#   Fullrig/   Amp und Box in einem Capture
#   Amp/       nur der Amp
#   IR/        Impulsantworten
#   Pedals/    Pedale
#   Outboard/  Rackgeräte
#
# Keine Unterordner, die Dateinamen enthalten Bank und Variante schon. Die
# Geräteart kommt aus der Metadatei (*.t3k.json), weil NAM/Models Amps und
# Fullrigs enthält. Ohne Metadatei entscheidet der Ordner.
#
# Metadateien und Vorschaubilder werden nicht kopiert. Die Quelle bleibt
# unverändert, es wird nur kopiert.

set -euo pipefail

SRC="${1:-$HOME/Documents/NAMplify}"
DST="${2:-}"

if [ ! -d "$SRC" ]; then
  echo "Die Bibliothek liegt nicht unter: $SRC" >&2
  exit 1
fi

if [ -z "$DST" ]; then
  # Neben die Quelle, mit Datum, damit ein zweiter Lauf den ersten nicht
  # überschreibt.
  DST="$HOME/Desktop/$(basename "$SRC") - Ordner $(date +%Y-%m-%d_%H%M)"
fi

files=0

# Zielordner einer Datei: erst die Metadatei, dann der Ordner.
zielordner() {
  local file="$1"
  local dir base stem note gear

  dir="$(dirname "$file")"
  base="$(basename "$file")"
  stem="${base%.*}"

  # Die Metadatei neben der Datei gilt, sonst irgendeine aus der Bank (eine Bank
  # hat genau eine Geräteart).
  note="$dir/$stem.t3k.json"
  if [ ! -f "$note" ]; then
    note="$(find "$dir" -maxdepth 1 -name '*.t3k.json' -print -quit 2>/dev/null || true)"
  fi

  gear=""
  if [ -n "$note" ] && [ -f "$note" ]; then
    gear="$(sed -n 's/.*"gear"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$note" | head -1)"
  fi

  case "$gear" in
    amp-cab|full-rig) echo "Fullrig"; return;;
    amp)              echo "Amp";     return;;
    cab|ir)           echo "IR";      return;;
    pedal)            echo "Pedals";  return;;
    outboard)         echo "Outboard"; return;;
  esac

  # Keine Metadatei: der Ordner der Bibliothek entscheidet grob.
  case "$file" in
    */NAM/IRs/*)      echo "IR";;
    */NAM/Pedals/*)   echo "Pedals";;
    */NAM/Outboard/*) echo "Outboard";;
    *)                echo "Amp";;
  esac
}

while IFS= read -r -d '' file; do
  rel="${file#"$SRC"}"
  rel="${rel#/}"
  # Punktordner (.audition) gehören zum Programm.
  case "$rel" in .*|*/.*) continue;; esac

  base="$(basename "$file")"
  case "$base" in
    *.nam|*.wav|*.NAM|*.WAV) ;;
    *) continue;;
  esac
  # Aufnahmen sind keine Captures.
  case "$rel" in Recordings/*) continue;; esac

  ordner="$(zielordner "$file")"
  mkdir -p "$DST/$ordner"

  # Gleiche Namen aus verschiedenen Bänken durchnummerieren statt überschreiben.
  target="$DST/$ordner/$base"
  if [ -e "$target" ]; then
    stem="${base%.*}"; ext="${base##*.}"; n=2
    while [ -e "$DST/$ordner/$stem ($n).$ext" ]; do n=$((n + 1)); done
    target="$DST/$ordner/$stem ($n).$ext"
  fi

  cp -p "$file" "$target"
  files=$((files + 1))
done < <(find "$SRC" -type f -print0)

# Leere Ordner entfernen.
find "$DST" -type d -empty -delete 2>/dev/null || true
mkdir -p "$DST"

# Kurzbericht
bericht=""
for ordner in Fullrig Amp IR Pedals Outboard; do
  if [ -d "$DST/$ordner" ]; then
    n=$(find "$DST/$ordner" -type f | wc -l | tr -d ' ')
    bericht="$bericht$ordner $n · "
  fi
done

echo "${bericht%· }($files Dateien)"
echo "$DST"
