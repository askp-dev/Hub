#!/bin/bash
# Zips a plugin build for the Hub and adds it to manifest.json.
#   tools/package-plugin.sh <id> <version> <artefacts dir> [mac|win]
# e.g. (from the Hub repo, after a Release build of V/A):
#   tools/package-plugin.sh va 0.2.0 "../V-A/build/VA_artefacts/Release"
# The zip holds the format folders exactly as JUCE writes them (VST3/, AU/, CLAP/),
# with only the bundles manifest.json names for that plugin. Upload it with the
# command this prints, then commit manifest.json: the Hub picks it up on its next check.
# EXTRA="path1 path2" adds files or folders at the zip root (licences, credits); the
# Hub only installs the bundles, so these ride along for anyone who opens the zip.
set -euo pipefail
ID="${1:?plugin id from manifest.json}"; VERSION="${2:?version}"; ART="${3:?artefacts dir}"
PLATFORM="${4:-mac}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HERE/dist"; mkdir -p "$OUT"
ZIP="$OUT/$ID-$VERSION-$PLATFORM.zip"
rm -f "$ZIP"

# bundle names for this plugin, as "VST3/VA.vst3" lines
ITEMS=$(python3 - "$HERE/manifest.json" "$ID" "$PLATFORM" <<'PY'
import json, sys
m = json.load(open(sys.argv[1])); p = next(x for x in m["plugins"] if x["id"] == sys.argv[2])
folders = {"vst3": "VST3", "au": "AU", "clap": "CLAP"}
for k, name in p["bundles"].items():
    if sys.argv[3] == "win" and k == "au": continue
    print(f"{folders[k]}/{name}")
PY
)
cd "$ART"
FOUND=()
while read -r item; do
    if [ -e "$item" ]; then FOUND+=("$item"); else echo "note: $item not in this build, skipped"; fi
done <<< "$ITEMS"
[ ${#FOUND[@]} -gt 0 ] || { echo "no bundles found in $ART"; exit 1; }
# -y keeps symlinks as symlinks (macOS bundles may contain them); -X drops extra attributes
zip -qryX "$ZIP" "${FOUND[@]}"
for extra in ${EXTRA:-}; do
    (cd "$(dirname "$extra")" && zip -qrX "$ZIP" "$(basename "$extra")")
done

SHA=$(shasum -a 256 "$ZIP" | cut -d' ' -f1)
SIZE=$(wc -c < "$ZIP" | tr -d ' ')
TAG="$ID-v$VERSION"
URL="https://github.com/askp-dev/hub/releases/download/$TAG/$(basename "$ZIP")"
FORMATS=$(printf '%s\n' "${FOUND[@]}" | cut -d/ -f1 | tr 'A-Z' 'a-z' | paste -sd, -)
python3 - "$HERE/manifest.json" "$ID" "$VERSION" "$PLATFORM" "$URL" "$SHA" "$SIZE" "$FORMATS" <<'PY'
import json, sys, datetime
path, pid, ver, plat, url, sha, size, fmts = sys.argv[1:]
m = json.load(open(path)); p = next(x for x in m["plugins"] if x["id"] == pid)
if p.get("version") != ver:   # a new version: other platforms' downloads are now stale
    p["downloads"] = {}
p["version"] = ver
p["released"] = datetime.date.today().isoformat()
p.setdefault("downloads", {})[plat] = {"url": url, "sha256": sha, "size": int(size),
                                           "formats": fmts.split(",")}
json.dump(m, open(path, "w"), indent=2); open(path, "a").write("\n")
PY
echo "wrote $ZIP ($SIZE bytes, sha256 $SHA)"
echo "manifest.json updated. Next:"
echo "  gh release create $TAG \"$ZIP\" -R askp-dev/hub --title \"$ID $VERSION\" --notes \"...\"   (or upload with gh release upload if the tag exists)"
echo "  git commit -am \"$ID $VERSION ($PLATFORM)\" && git push"
