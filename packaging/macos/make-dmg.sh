#!/bin/bash
# Builds the Hub DMG people download:  packaging/macos/make-dmg.sh <path to fueledby Hub.app> [out dir]
# Unsigned for now: the app gets an ad-hoc signature so it runs on Apple Silicon, and
# testers open it once via System Settings > Privacy & Security > Open Anyway.
# With a Developer ID (see docs/DESIGN.md): set SIGN_ID="Developer ID Application: ..."
# and NOTARY_PROFILE=<notarytool keychain profile> to sign, notarize and staple.
set -euo pipefail
APP="${1:?path to fueledby Hub.app}"; OUT="${2:-dist}"
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP/Contents/Info.plist")
mkdir -p "$OUT"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
cp -R "$APP" "$WORK/"
if [ -n "${SIGN_ID:-}" ]; then
    codesign --force --deep --options runtime --timestamp -s "$SIGN_ID" "$WORK/$(basename "$APP")"
else
    codesign --force --deep -s - "$WORK/$(basename "$APP")"
fi
ln -s /Applications "$WORK/Applications"
DMG="$OUT/fueledby-Hub-macOS.dmg"
rm -f "$DMG"
hdiutil create -volname "fueledby Hub $VERSION" -srcfolder "$WORK" -ov -format UDZO "$DMG" >/dev/null
if [ -n "${SIGN_ID:-}" ] && [ -n "${NOTARY_PROFILE:-}" ]; then
    codesign -s "$SIGN_ID" --timestamp "$DMG"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
fi
echo "wrote $DMG"
