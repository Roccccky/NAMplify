#!/usr/bin/env bash
# NAMplify unter Linux für den eigenen Benutzer einrichten, ohne Root.
#
#   ./install.sh              einrichten
#   ./install.sh --check      nur prüfen, ob alle Abhängigkeiten da sind
#   ./install.sh --uninstall  entfernen (die Bibliothek bleibt)
#
# Ziele:
#   Programm      ~/.local/lib/namplify/NAMplify, Link in ~/.local/bin
#   VST3          ~/.vst3/NAMplify.vst3        (dort suchen Reaper, Bitwig,
#   Ardour)
#   Startmenü     ~/.local/share/applications/namplify.desktop
#   Symbol        ~/.local/share/icons/hicolor/128x128/apps/namplify.png
#
# Die .desktop-Datei meldet auch das URL-Schema namplify:// an, darüber kommt
# die TONE3000-Anmeldung aus dem Browser zurück.
#
# Ein Treiber ist nicht nötig. JUCE nutzt ALSA und JACK, PipeWire bedient
# beides. Die einzige Laufzeitabhängigkeit, die fehlen kann, ist WebKitGTK für
# die UI.

set -euo pipefail
cd "$(dirname "$0")"

LIB="$HOME/.local/lib/namplify"
BIN="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor/128x128/apps"
VST3="$HOME/.vst3"

have_lib () { ldconfig -p 2>/dev/null | grep -q "$1"; }

check () {
  local missing=0
  for entry in "libwebkit2gtk-4.1.so:WebKitGTK 4.1  (Debian/Ubuntu: libwebkit2gtk-4.1-0 · Fedora: webkit2gtk4.1)" \
               "libgtk-3.so:GTK 3  (libgtk-3-0 · gtk3)" \
               "libcurl.so:libcurl  (libcurl4 · libcurl)" \
               "libasound.so:ALSA  (libasound2 · alsa-lib)" \
               "libfreetype.so:FreeType  (libfreetype6 · freetype)"; do
    local lib="${entry%%:*}" what="${entry#*:}"
    if have_lib "$lib"; then
      echo "  ok       $what"
    else
      echo "  MISSING  $what"
      missing=1
    fi
  done
  if ! have_lib "libjack.so"; then
    echo "  optional JACK (libjack) for the lowest latency, PipeWire provides it via pipewire-jack"
  fi
  return $missing
}

case "${1:-}" in
  --check)
    echo "NAMplify needs:"
    check && echo "All there." || { echo "Install what is missing, then run ./install.sh again."; exit 1; }
    exit 0
    ;;
  --uninstall)
    rm -rf "$LIB" "$VST3/NAMplify.vst3"
    rm -f "$BIN/NAMplify" "$APPS/namplify.desktop" "$ICONS/namplify.png"
    command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
    echo "NAMplify removed. Your library in ~/Documents/NAMplify stays where it is."
    exit 0
    ;;
esac

echo "NAMplify needs:"
check || echo "Something is missing. NAMplify is installed anyway, install it before starting."

mkdir -p "$LIB" "$BIN" "$APPS" "$ICONS" "$VST3"

install -m 755 NAMplify "$LIB/NAMplify"
ln -sf "$LIB/NAMplify" "$BIN/NAMplify"

rm -rf "$VST3/NAMplify.vst3"
cp -R NAMplify.vst3 "$VST3/"

install -m 644 namplify.png "$ICONS/namplify.png"
sed "s|@BIN@|$LIB/NAMplify|" namplify.desktop > "$APPS/namplify.desktop"

# URL-Schema anmelden, sonst kommt die TONE3000-Anmeldung nicht zurück.
command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
command -v xdg-mime >/dev/null && xdg-mime default namplify.desktop x-scheme-handler/namplify || true

echo
echo "Done. NAMplify is in your application menu; the VST3 is in ~/.vst3."
case ":$PATH:" in *":$BIN:"*) ;; *) echo "Note: $BIN is not in your PATH. Start it from the menu or add it." ;; esac
