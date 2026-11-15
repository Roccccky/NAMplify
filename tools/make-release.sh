#!/usr/bin/env bash
# Release-Build für macOS: universal bauen, prüfen, verpacken.
#
#   tools/make-release.sh
#
# Ergebnis ist ein Installer unter release/, z. B.
# release/NAMplify-2.0.0-macOS.pkg. Er installiert drei Teile, jeder abwählbar:
# - NAMplify.app        nach /Applications
# - NAMplify.vst3       nach /Library/Audio/Plug-Ins/VST3
# - NAMplify.component  nach /Library/Audio/Plug-Ins/Components
#
# Ad-hoc signiert, nicht notarisiert. Auf anderen Rechnern hält Gatekeeper es
# an, dort hilft Rechtsklick und Öffnen.
#
# Windows wird hier nicht gebaut, das macht die CI
# (.github/workflows/release.yml).

set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="$(sed -n 's/^project *(NAMplify VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$VERSION" ] || { echo "Keine Versionsnummer in CMakeLists.txt gefunden." >&2; exit 1; }

ART="build/plugin/NAMplify_artefacts/Release"
OUT="release"
STAGE="build/release-stage"

echo "── NAMplify $VERSION, Release-Build für macOS"
echo

# 1. Bauen, universal.
tools/build-and-test.sh universal

for p in "$ART/Standalone/NAMplify.app" "$ART/VST3/NAMplify.vst3" "$ART/AU/NAMplify.component"; do
  [ -d "$p" ] || { echo "Fehlt: $p" >&2; exit 1; }
done

# Prüfen, ob wirklich beide Architekturen drin sind. Der Schalter kann im
# CMake-Cache hängen bleiben.
BIN="$ART/Standalone/NAMplify.app/Contents/MacOS/NAMplify"
ARCHS="$(lipo -archs "$BIN")"
echo
echo "   Architekturen: $ARCHS"
case "$ARCHS" in
  *arm64*x86_64*|*x86_64*arm64*) ;;
  *) echo "   Nicht universal, abgebrochen." >&2; exit 1 ;;
esac

# 2. Bereitlegen
rm -rf "$STAGE" && mkdir -p "$STAGE"/{app,vst3,au,pkgs}
cp -R "$ART/Standalone/NAMplify.app"   "$STAGE/app/"
cp -R "$ART/VST3/NAMplify.vst3"        "$STAGE/vst3/"
cp -R "$ART/AU/NAMplify.component"     "$STAGE/au/"

# Lizenz und Hinweise zu Fremdcode (AGPL, MIT) kommen in die Resources jedes
# Bündels. Danach neu ad-hoc signieren, sonst passt die Signatur nicht mehr zum
# Inhalt.
if [ -f LICENSE ] && [ -f THIRD_PARTY_NOTICES.md ]; then
  for b in "$STAGE/app/NAMplify.app" "$STAGE/vst3/NAMplify.vst3" "$STAGE/au/NAMplify.component"; do
    cp LICENSE "$b/Contents/Resources/LICENSE.txt"
    cp THIRD_PARTY_NOTICES.md "$b/Contents/Resources/"
    codesign --force --sign - "$b"
  done
  mkdir -p "$STAGE/resources"
  cp LICENSE "$STAGE/resources/LICENSE.txt"
  LICENSE_XML='<license file="LICENSE.txt"/>'
else
  echo "   LICENSE oder THIRD_PARTY_NOTICES.md fehlt, Paket ohne Lizenz (nur für den Eigengebrauch)." >&2
  mkdir -p "$STAGE/resources"
  LICENSE_XML=''
fi

# 3. Je Ziel ein Teilpaket. productbuild setzt sie zu einem Installer zusammen,
# in dem man einzelne Teile abwählen kann.
pkgbuild --quiet --root "$STAGE/app"  --identifier de.martinmaiwald.namplify.app \
         --version "$VERSION" --install-location /Applications \
         "$STAGE/pkgs/app.pkg"
pkgbuild --quiet --root "$STAGE/vst3" --identifier de.martinmaiwald.namplify.vst3 \
         --version "$VERSION" --install-location /Library/Audio/Plug-Ins/VST3 \
         "$STAGE/pkgs/vst3.pkg"
pkgbuild --quiet --root "$STAGE/au"   --identifier de.martinmaiwald.namplify.au \
         --version "$VERSION" --install-location /Library/Audio/Plug-Ins/Components \
         "$STAGE/pkgs/au.pkg"

# 4. Zusammenbau
cat > "$STAGE/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>NAMplify $VERSION</title>
    <organization>de.martinmaiwald</organization>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    $LICENSE_XML

    <choices-outline>
        <line choice="app"/>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>

    <choice id="app"  title="NAMplify"                  description="Das Programm für sich allein (/Applications).">
        <pkg-ref id="de.martinmaiwald.namplify.app"/>
    </choice>
    <choice id="vst3" title="VST3-Plugin"               description="Für jede Aufnahmesoftware, die VST3 kann.">
        <pkg-ref id="de.martinmaiwald.namplify.vst3"/>
    </choice>
    <choice id="au"   title="Audio-Unit-Plugin"         description="Für Logic, GarageBand und alles andere von Apple.">
        <pkg-ref id="de.martinmaiwald.namplify.au"/>
    </choice>

    <pkg-ref id="de.martinmaiwald.namplify.app"  version="$VERSION" onConclusion="none">app.pkg</pkg-ref>
    <pkg-ref id="de.martinmaiwald.namplify.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
    <pkg-ref id="de.martinmaiwald.namplify.au"   version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
</installer-gui-script>
XML

mkdir -p "$OUT"
PKG="$OUT/NAMplify-$VERSION-macOS.pkg"
productbuild --quiet --distribution "$STAGE/distribution.xml" \
             --resources "$STAGE/resources" \
             --package-path "$STAGE/pkgs" "$PKG"

# 5. Prüfsummen
( cd "$OUT" && shasum -a 256 "$(basename "$PKG")" > "$(basename "$PKG").sha256" )

echo
echo "── fertig"
echo "   $PKG   ($(du -h "$PKG" | cut -f1))"
echo "   $(cat "$PKG.sha256")"
echo
echo "   Ad-hoc unterschrieben. Auf einem fremden Rechner: Rechtsklick → Öffnen."
