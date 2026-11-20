# askp Hub

One app to install, update and remove askp plugins (V/A, V/A Lab, Sector, Sibil, Taste, Grille). Share one
DMG (Mac) or setup EXE (Windows); people pick plugins and formats inside the app.

![Hub, light](docs/hub-light.png)

How it works, where files go, and what signing needs: [docs/DESIGN.md](docs/DESIGN.md).

## Build

```bash
# Mac (reuse V/A's JUCE copy to save disk):
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
      -DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/../V-A/build/_deps/juce-src"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
open "build/Hub_artefacts/Release/askp Hub.app"

packaging/macos/make-dmg.sh "build/Hub_artefacts/Release/askp Hub.app"   # -> dist/askp-Hub-macOS.dmg
```

Linux builds too (the cloud checks run there): install the JUCE Linux deps plus libcurl.

## Publish a plugin version

1. Build the plugin in Release (universal on Mac: `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and
   `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`).
2. From this repo: `tools/package-plugin.sh va 0.2.0 ../V-A/build/VA_artefacts/Release`
   (add `win` as a 4th argument for a Windows build). Add absolute file or directory
   paths after the platform to include credits and licences. All listed formats must exist. It zips the bundles into `dist/`,
   and writes the URL, size, SHA-256 and formats into `manifest.json`.
3. Upload the zip with the `gh release create ...` line it prints (or drag it into a new
   release on GitHub named the same as the tag).
4. Commit and push `manifest.json`. Every Hub sees the update on its next check
   (at launch, or Refresh).

Before pushing the manifest, verify every download, its SHA-256 and size, bundle
versions, Windows x64 binaries, and Grille's bundled IR credits:

```bash
python3 tools/verify-manifest.py --require-platform mac --require-platform win
```

Plugin releases must use `gh release create ... --latest=false`. The Hub installers
use `releases/latest`, so the Hub release must remain marked Latest.

To publish the Hub itself, upload `askp-Hub-macOS.dmg` / `askp-Hub-Windows-Setup.exe` to a
`hub-v<version>` release and bump `hub.version` in `manifest.json`; older Hubs then show a
download banner.

## Testing without touching your plug-in folders

```bash
ASKP_HUB_ROOT=/tmp/hub-sandbox "build/Hub_artefacts/Release/askp Hub.app/Contents/MacOS/askp Hub" \
    --manifest /path/to/test-manifest.json
```

`ASKP_HUB_ROOT` puts every plug-in folder and the Hub's data under that folder. `--manifest`
(or `ASKP_HUB_MANIFEST`) reads another list, local file or URL. `--snapshot out.png [--dark]`
renders the window to a PNG and quits.

## For testers

**Mac:** open the DMG, drag askp Hub to Applications, double-click it. The first time macOS
says it can't check the app: press Done, open System Settings > Privacy & Security, scroll
down and press **Open Anyway**. After that it opens normally. Pick a plugin, press Install,
then fully quit and reopen your DAW.

**Windows:** run askp-Hub-Windows-Setup.exe. If Windows says it protected your PC, click
**More info > Run anyway**. Then open askp Hub from the Start menu.
