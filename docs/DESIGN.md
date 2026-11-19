# askp Hub: design

One small app that people download once (a DMG on Mac, a setup EXE on Windows). It lists
every askp plugin, installs the ones they pick, tells them when an update is out, and
removes plugins cleanly. Same idea as Arturia Software Center or Native Access, minus
accounts and licences for now.

## How it fits together

```
 askp-dev/hub (public repo)
 ├── manifest.json            the catalogue: every plugin, latest version, download links
 └── Releases                 one release per plugin version, e.g. tag va-v0.2.0
     ├── va-0.2.0-mac.zip     VST3/VA.vst3 + AU/VA.component + CLAP/VA.clap
     └── va-0.2.0-win.zip     VST3/VA.vst3 + CLAP/VA.clap

 askp Hub app ──GET──> raw.githubusercontent.com/askp-dev/hub/main/manifest.json
              ──GET──> github.com/askp-dev/hub/releases/download/<tag>/<zip>
```

* The plugin repos (V-A, Sector, Taste, Sibil) stay private. Only built binaries go into
  the public Hub repo's Releases, because private release assets need a GitHub login.
* Publishing a version means uploading a zip and committing `manifest.json`. No server.
  If the files ever outgrow GitHub, the same layout works from Cloudflare R2 (free egress)
  by changing the URLs in the manifest.

## manifest.json (schema 1)

```json
{
  "schema": 1,
  "hub": { "version": "0.1.0", "download": { "mac": "<dmg url>", "win": "<exe url>" } },
  "plugins": [{
    "id": "va", "name": "V/A", "badge": "V/A",
    "tagline": "one or two lines",
    "version": "0.2.0", "released": "2026-11-19", "notes": "What's new text",
    "bundles": { "vst3": "VA.vst3", "au": "VA.component", "clap": "VA.clap" },
    "downloads": {
      "mac": { "url": "...", "sha256": "...", "size": 8421337, "formats": ["vst3", "au", "clap"] },
      "win": { "url": "...", "sha256": "...", "size": 6120044, "formats": ["vst3", "clap"] }
    }
  }]
}
```

* `id` never changes (it keys the install record). `badge` is the text on the card's tile.
* A plugin with no download for this platform shows **Coming soon**.
* `formats` on a download lists what the zip holds; the Hub only offers those.
* `schema` goes up only for changes old Hubs can't read. An old Hub then asks to be updated
  instead of misreading the list.
* `hub.version` newer than the running app shows a banner linking to the new Hub.
* `tools/package-plugin.sh` writes all of this for you (see README).

## What the app does

| Action | Steps |
| --- | --- |
| Install / Update | download to `Downloads/` (with progress and Cancel), check size and SHA-256, unzip to `Staging/`, check every chosen bundle is there, then swap each bundle in (old copy kept as `.hub-old` until the new one is in place, restored on failure), record it in `installed.json`, delete the zip |
| Change formats | untick a format chip and press **Apply**: the chosen formats are installed, unticked ones removed |
| Remove | deletes the bundles from the Hub's folders and the install record |
| Detect | a bundle found in the Hub's folders but not installed by the Hub is shown with the version from its `Info.plist`; a copy in the system folders (e.g. from the old V/A .pkg) is shown as "older copy in /Library/..." |

After installing on Mac it clears the quarantine flag on the bundle and restarts
`AudioComponentRegistrar`, so Logic and auval see a new AU without a reboot. FL Studio still
needs to be fully quit and reopened (or rescanned) to load a new build.

The last good manifest is cached, so the app still shows your plugins offline.

### Where plugins go

| | VST3 | AU | CLAP | Admin needed? |
| --- | --- | --- | --- | --- |
| Mac | `~/Library/Audio/Plug-Ins/VST3` | `~/Library/Audio/Plug-Ins/Components` | `~/Library/Audio/Plug-Ins/CLAP` | No |
| Windows | `C:\Program Files\Common Files\VST3\askp` | | `C:\Program Files\Common Files\CLAP\askp` | Only for the one-time Hub setup |

* Mac uses the per-user folders: no password prompts, and FL Studio, Ableton, Logic,
  Reaper and Bitwig all scan them. (Arturia and NI install to `/Library` with a privileged
  helper; that's only worth it if we need all-users installs.)
* Windows uses the standard shared folders, which need admin to write. The Hub's setup EXE
  runs once as admin and creates an `askp` subfolder in each that normal users can write
  to (`packaging/windows/hub.iss`), so the Hub never asks for admin again. Every Windows DAW
  scans subfolders of the VST3 folder.

### Safety

* HTTPS only for real lists; every zip is checked against the SHA-256 in the manifest.
* Bundle names from the manifest can't contain path separators; zip entries can't contain
  `..` or absolute paths. Nothing is touched until the whole zip is verified and unpacked.
* The Hub only ever writes to its own plugin folders and its data folder.

## Code

```
Source/core/Manifest.h      parse + validate the list, version compare (no UI)
Source/core/Installer.*     paths, download, verify, install, remove, status (no UI)
Source/app/StudioLook.h     the V/A "Studio white" look (light/dark)
Source/app/HubComponent.*   window: header, plugin cards, banner, footer
Source/app/Main.cpp         app, plus --snapshot / --install for headless checks
tests/hub_tests.cpp         install/update/cancel/bad checksum/format change/remove in a sandbox
```

JUCE 8.0.4, same as the plugins, so one toolchain and the same look code. The installer core
is UI-free and is what the tests drive.

## Signing: what you'd need, and when

Nothing below is needed for testers. It's needed before a public release, so that people
don't see security warnings.

### Mac
* **Apple Developer Program**, 99 USD a year, individual is fine.
* From it, a **Developer ID Application** certificate. Use it to sign the Hub app *and* every
  plugin bundle (VST3, AU, CLAP), with the hardened runtime.
* **Notarization** with `xcrun notarytool` (free with the membership), then staple the DMG.
  `packaging/macos/make-dmg.sh` already does this when `SIGN_ID` and `NOTARY_PROFILE` are set.
* A **Developer ID Installer** certificate is only needed if you keep shipping .pkg installers.

Until then the DMG is ad-hoc signed. Testers open the Hub once like this: double-click it,
press Done on the warning, then **System Settings > Privacy & Security > Open Anyway**.
Plugins installed by the Hub load without any prompt, because the Hub writes them itself
(no quarantine flag) and they carry the build's ad-hoc signature.

### Windows
* A code-signing certificate. Cheapest current route is **Azure Trusted Signing**
  (about 10 USD a month, individuals in the US and Canada qualify). A classic OV
  certificate from a CA is about 200 to 400 USD a year and now needs a hardware token.
* Sign the setup EXE and `askp Hub.exe` (`signtool`). Plugin DLLs can be signed too but
  hosts don't require it.

Until then, testers see "Windows protected your PC" on the setup EXE: click **More info >
Run anyway**.

## Later

* Self-update of the Hub itself (Sparkle on Mac, WinSparkle on Windows) instead of the
  download banner.
* Presets and content packs as extra downloads per plugin.
* Beta channel: a second manifest (`manifest-beta.json`) testers switch to in settings.
* Licences and sign-in, if any plugin becomes paid.
* All-users install on Mac via a privileged helper.
