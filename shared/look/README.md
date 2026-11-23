# fueledby look and themes

`FueledbyLook.h` is the one look every fueledby product draws with: the "Studio" LookAndFeel, the colour
tokens, and the theme engine. This folder is the canonical copy. Each product carries a copy in
`<source dir>/look/`, included through its own `StudioLook.h` wrapper:

| Product | Wrapper | Namespace |
| --- | --- | --- |
| V/A | `Source/plugin/StudioLook.h` | `va::ui` |
| Sector | `Source/plugin/StudioLook.h` | `sct::ui` |
| Sibil | `Source/plugin/StudioLook.h` | `sib::ui` |
| Grille | `Source/plugin/StudioLook.h` | `grl::ui` |
| Taste | `Source/plugin/StudioLook.h` | `taste::ui` |
| fueledby Hub | `Source/app/StudioLook.h` | `hub::ui` |

Change the look here, then copy it into each product and rebuild:

```sh
tools/sync-look.sh ../V-A/Source/plugin
tools/sync-look.sh --check ../V-A/Source/plugin   # is that copy current?
```

Each product also compiles `look/FueledbyFonts.cpp` (Inter, embedded).

## Theme files (`.fueledbytheme`)

JSON, at most 16 KB, data only. Unknown keys are ignored and numbers are clamped, so older
plugins can open newer files.

```json
{
  "format": 1,
  "id": "askthestoryteller.ember",
  "name": "Ember",
  "author": "askthestoryteller",
  "version": "1.0.0",
  "license": "CC0-1.0",
  "light": { "bg": "#ECE6DE", "panel": "#F7F2EA", "ink": "#2A1A12", "accent": "#D9622B", "shadow": 0.07 },
  "dark":  { "bg": "#1C1512", "panel": "#26201B", "ink": "#F1E6DA", "accent": "#F08A4B" },
  "shape": { "knob": "dot", "fader": "ticks", "corner": 9, "line": 1.0 },
  "font":  { "family": "inter", "scale": 1.0 }
}
```

- `id`: `author.name`, lower case `a-z 0-9 . _ -`. Ids starting `fueledby.` are reserved for built-ins.
- `light` / `dark`: one or both. With both, the plugins' light/dark button switches between them.
  Tokens: `bg footer panel surface line grid track tick rim ink muted ghost onInk mute solo alert
  accent` as `#RRGGBB`, plus `shadow` (0 to 0.6). A missing token falls back to Studio's value
  for that mode; a missing `accent` means "same as ink" (monochrome).
- `shape.knob`: `arc` (Studio), `cap` (dark cap, value ring), `dot` (light cap, accent dot),
  `flat` (no cap). `shape.fader`: `ticks` or `plain`. `corner`: 0 to 14 (Studio 7). `line`: 0.75 to 2.
- `font.family`: `system`, `inter` (embedded) or `mono`. `scale`: 0.9 to 1.15.

Built-in themes: Studio, Paper, Graphite, Moss and Console (dark only).

## Where the choice lives

One user-level setting shared by every fueledby plugin and the Hub, never saved in a DAW project:

| | Choice | Installed themes |
| --- | --- | --- |
| macOS | `~/Library/Application Support/fueledby/Look.settings` | `.../fueledby/Themes/*.fueledbytheme` |
| Windows | `%APPDATA%\fueledby\Look.settings` | `%APPDATA%\fueledby\Themes\*.fueledbytheme` |

`Look.settings` is `{"theme": id, "dark": bool, "counter": n}`. Each open window has a
`ThemeFollower` that checks the file's timestamp about once a second and repaints when it
changes, so a theme chosen in one plugin (or the Hub) shows everywhere. On first run a product
copies its old per-plugin `darkMode` preference into the shared file.

Overrides for tests and snapshots: `FUELEDBY_LOOK_DIR` (use another folder), `FUELEDBY_THEME` (show a
theme id without saving).

## Tests

`look_tests` (built by this repo's CMake) checks parsing, contrast of every built-in palette,
migration, persistence, live reload and user theme files. Run it under `xvfb-run` on Linux; pass a
path to also render every built-in theme to a PNG.

Inter: SIL Open Font License 1.1, see `Inter-LICENSE.txt`. `tools/embed_fonts.py` rebuilds
`FueledbyFonts.cpp` from the TTFs.
