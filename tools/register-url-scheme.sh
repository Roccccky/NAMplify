#!/usr/bin/env bash
# Trägt das URL-Schema in die Info.plist der Standalone ein.
#
#   tools/register-url-scheme.sh <Info.plist> <Schema> <Kennung>
#
# juce_add_plugin kennt keine CFBundleURLTypes. Nur die Standalone darf das
# Schema anmelden, ein VST3- oder AU-Bündel würde es sonst belegen, ohne es
# bedienen zu können.
#
# Der Eintrag wird erst gelöscht und dann neu geschrieben, damit ein zweiter
# Build nicht daran scheitert.

set -euo pipefail

PLIST="$1"
SCHEME="$2"
IDENT="$3"

[ -f "$PLIST" ] || { echo "Info.plist fehlt: $PLIST" >&2; exit 1; }

PB=/usr/libexec/PlistBuddy

"$PB" -c "Delete :CFBundleURLTypes" "$PLIST" >/dev/null 2>&1 || true
"$PB" -c "Add :CFBundleURLTypes array" \
      -c "Add :CFBundleURLTypes:0 dict" \
      -c "Add :CFBundleURLTypes:0:CFBundleURLName string $IDENT" \
      -c "Add :CFBundleURLTypes:0:CFBundleURLSchemes array" \
      -c "Add :CFBundleURLTypes:0:CFBundleURLSchemes:0 string $SCHEME" \
      "$PLIST" >/dev/null
