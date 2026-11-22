#!/bin/sh
# Copies the canonical askp look (shared/look/) into a product, so every askp plugin and the Hub
# draw with the same code and read the same theme setting.
#
#   tools/sync-look.sh <dest>          e.g. tools/sync-look.sh ../V-A/Source/plugin
#   tools/sync-look.sh --check <dest>  exit 1 if <dest>/look differs from shared/look
#
# <dest> is the folder holding the product's StudioLook.h wrapper; files land in <dest>/look/.
# Known products: V-A, Sector, Sibil, Grille, Taste (Source/plugin) and this Hub (Source/app).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
src="$here/shared/look"
check=0
if [ "${1:-}" = "--check" ]; then check=1; shift; fi
dest="${1:?usage: sync-look.sh [--check] <folder with StudioLook.h>}"
[ -f "$dest/StudioLook.h" ] || { echo "no StudioLook.h in $dest" >&2; exit 2; }
for f in AskpLook.h AskpFonts.cpp Inter-LICENSE.txt; do
    if [ $check = 1 ]; then
        cmp -s "$src/$f" "$dest/look/$f" || { echo "out of date: $dest/look/$f" >&2; exit 1; }
    else
        mkdir -p "$dest/look"
        cp "$src/$f" "$dest/look/$f"
    fi
done
[ $check = 1 ] && echo "look is current in $dest" || echo "synced look into $dest/look"
