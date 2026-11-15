#!/usr/bin/env bash
# Baut NAMplify Auspacken.app, die App um extract-library.sh.
#
# Ein AppleScript-Bündel (osacompile), das sich per Doppelklick starten und mit
# einem Ordner beladen lässt. Das Skript liegt in Contents/Resources, die App
# läuft also für sich allein.

# tools/make-extractor-app.sh [Zielordner]
#
# Ohne Argument landet die App im Projektordner. tools/build-and-test.sh all
# legt sie zusätzlich neben die Artefakte.

set -euo pipefail
cd "$(dirname "$0")/.."

DEST="${1:-.}"
mkdir -p "$DEST"
APP="$DEST/NAMplify Auspacken.app"

rm -rf "$APP"
osacompile -o "$APP" tools/extractor.applescript
cp tools/extract-library.sh "$APP/Contents/Resources/"
chmod +x "$APP/Contents/Resources/extract-library.sh"

# Ad-hoc signieren, sonst fragt macOS bei jedem Start neu nach Zugriff auf
# Dokumente und Schreibtisch.
if [ "$(uname)" = "Darwin" ]; then
  codesign --force --sign - --timestamp=none "$APP" >/dev/null 2>&1 || true
fi

echo "$APP"
