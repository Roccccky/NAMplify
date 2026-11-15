#!/usr/bin/env bash
# Bauen und testen. Ohne Argumente: Kern und Tests (schnell).
#
#   tools/build-and-test.sh            nur Kern + Tests, arm64
#   tools/build-and-test.sh all        alles, inklusive Plugin
#   tools/build-and-test.sh universal  alles, Universal (arm64 + x86_64)
#
# Ein Argument nach dem Modus filtert die Tests:
#   tools/build-and-test.sh core Hall

set -euo pipefail
cd "$(dirname "$0")/.."

MODE="${1:-core}"
FILTER="${2:-}"

# Jeder Modus setzt NAMPLIFY_BUILD_PLUGIN ausdrücklich. Sonst bleibt OFF aus
# einem vorherigen core-Lauf im Cache, und all baut nur den Kern, meldet aber
# die alten Plugin-Artefakte.
case "$MODE" in
  core)
    cmake -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DNAMPLIFY_BUILD_PLUGIN=OFF \
      -DCMAKE_OSX_ARCHITECTURES=arm64
    ;;
  all)
    cmake -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DNAMPLIFY_BUILD_PLUGIN=ON \
      -DCMAKE_OSX_ARCHITECTURES=arm64
    ;;
  universal)
    cmake -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DNAMPLIFY_BUILD_PLUGIN=ON \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
    ;;
  *)
    echo "Unbekannter Modus: $MODE  (core | all | universal)" >&2
    exit 2
    ;;
esac

cmake --build build

if [ -x build/tests/namplify_tests ]; then
  echo
  ./build/tests/namplify_tests "$FILTER"
fi

if [ "$MODE" != "core" ]; then
  echo
  echo "Artefakte:"
  # Mit Datum, damit man sieht, ob das Artefakt frisch ist.
  APP="build/plugin/NAMplify_artefacts/Release/Standalone/NAMplify.app/Contents/MacOS/NAMplify"
  if [ ! -x "$APP" ]; then
    echo "  keine, das Plugin wurde nicht gebaut." >&2
    exit 1
  fi
  ls -ldT build/plugin/NAMplify_artefacts/Release/*/ | sed 's/^/  /'

  # Sind die NAM-Architekturen im fertigen Programm?
  #
  # Sie registrieren sich über statische Objekte, auf die nichts direkt
  # verweist. Landen sie in einer statischen Bibliothek, kann der Linker sie
  # entfernen, und das Programm lädt kein Capture, ohne dass der Build warnt.
  # Deshalb wird es am fertigen Binary geprüft.
  #
  # Die Symbole einmal holen. Mit nm … | c++filt | grep -q in der Schleife
  # bricht grep die Pipe ab, c++filt stirbt an SIGPIPE, und pipefail meldet jede
  # Architektur als fehlend.
  SYMS="$(nm -U "$APP" 2>/dev/null | c++filt || true)"
  MISSING=""
  for ns in container convnet linear lstm sequential wavenet slimmable_wavenet; do
    case "$SYMS" in
      *"nam::$ns"*) ;;
      *) MISSING="$MISSING $ns" ;;
    esac
  done
  if [ -n "$MISSING" ]; then
    echo "  FEHLER: im gebauten Programm fehlen NAM-Architekturen:$MISSING" >&2
    echo "  So gebaut lädt kein Capture. Siehe plugin/CMakeLists.txt, Abschnitt" >&2
    echo "  „Der NAM-Kern muss im fertigen Programm ankommen\"." >&2
    exit 1
  fi
  echo "  (alle NAM-Architekturen sind drin)"

  # Das Auspack-Programm gehört zur Auslieferung: damit lassen sich die Captures
  # aus den .namplify-Bündeln in normale Ordner kopieren.
  tools/make-extractor-app.sh "build/plugin/NAMplify_artefacts/Release" >/dev/null \
    && echo "  (Auspack-Programm liegt dabei)"

  # Ad-hoc signieren. Die Linker-Signatur enthält weder Info.plist noch
  # versiegelte Ressourcen, dann kann macOS Berechtigungen keinem Programm
  # zuordnen und fragt bei jedem Start neu nach dem Mikrofon.
  if [ "$(uname)" = "Darwin" ]; then
    codesign --force --sign - --timestamp=none \
      "build/plugin/NAMplify_artefacts/Release/Standalone/NAMplify.app" >/dev/null 2>&1 \
      && echo "  (ad-hoc unterschrieben)"
  fi
fi
