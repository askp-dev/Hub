#pragma once
// askp shared look: the "Studio" LookAndFeel plus the theme engine used by every askp plugin and
// the askp Hub. CANONICAL COPY lives in the askp-dev/hub repo at shared/look/; plugins carry a copy
// made by tools/sync-look.sh. Edit it there, then sync, so the copies never drift.
//
// Each product includes it through its own StudioLook.h, which first defines
//   ASKP_LOOK_NS             the namespace to put everything in (e.g. va::ui)
//   ASKP_LOOK_LEGACY_FOLDER  the folder the old per-plugin dark-mode setting lived in (e.g. "V-A")
//
// Themes
//   A theme is a small JSON file (.askptheme): colours for a light and/or dark palette, knob and
//   fader style, corner size, line weight and a font family. The chosen theme and light/dark mode
//   are one user-level setting shared by every askp plugin and the Hub:
//     macOS    ~/Library/Application Support/askp/Look.settings   (+ Themes/*.askptheme)
//     Windows  %APPDATA%\askp\Look.settings                         (+ Themes\*.askptheme)
//   Open windows pick up a change made anywhere (another plugin, the Hub) within about a second:
//   each editor owns a ThemeFollower that polls the settings file's timestamp on the message thread.
//   Test/snapshot overrides: ASKP_LOOK_DIR (folder), ASKP_THEME (theme id, not saved).

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <optional>

#ifndef ASKP_LOOK_NS
 #error "Define ASKP_LOOK_NS (e.g. va::ui) before including AskpLook.h"
#endif
#ifndef ASKP_LOOK_LEGACY_FOLDER
 #define ASKP_LOOK_LEGACY_FOLDER ""
#endif

namespace ASKP_LOOK_NS {

// Colours are tokens, not literals: applyTheme() swaps the whole palette and every paint() reads
// the current value. "white" is the colour that sits on ink (knob caps, text on a filled button),
// so it flips with the mode; theme files call it "onInk".
namespace colours {
    // light Studio values until the theme engine runs
    inline juce::Colour bg { 0xffececec }, footer { 0xffe1e1e1 }, panel { 0xfff7f7f7 }, surface { 0xfffbfbfb },
                        line { 0xffdcdcdc }, grid { 0xffe6e6e6 }, track { 0xffe2e2e2 }, tick { 0xffc4c4c4 },
                        rim { 0xffd2d2d2 }, ink { 0xff1b1b1b }, muted { 0xff6b6b6b }, ghost { 0xff9a9a9a },
                        white { 0xffffffff };
    inline juce::Colour mute    { 0xffd9822b };   // slot muted
    inline juce::Colour solo    { 0xffd8b52a };   // slot soloed
    inline juce::Colour alert   { 0xffe0433a };   // clip / over
    inline juce::Colour accent  { 0xff1b1b1b };   // value arcs, fills, switches (Studio: same as ink)
    inline float shadow = 0.06f;                  // panel drop-shadow strength
}

//==============================================================================
// Theme model
enum class KnobStyle  { arc, cap, dot, flat };
enum class FaderStyle { ticks, plain };
enum class FontFamily { system, inter, mono };

namespace theme {

// Token order = file key order. Index into Palette::colour.
inline constexpr int numTokens = 17;
inline constexpr const char* tokenNames[numTokens] = {
    "bg", "footer", "panel", "surface", "line", "grid", "track", "tick", "rim",
    "ink", "muted", "ghost", "onInk", "mute", "solo", "alert", "accent" };

inline juce::Colour* tokenTarget(int i) {
    using namespace colours;
    juce::Colour* t[numTokens] = { &bg, &footer, &panel, &surface, &line, &grid, &track, &tick, &rim,
                                   &ink, &muted, &ghost, &white, &mute, &solo, &alert, &accent };
    return t[i];
}

struct Palette {
    std::array<std::optional<juce::Colour>, numTokens> colour {};
    std::optional<float> shadow;
};

struct Shape {
    KnobStyle  knob   = KnobStyle::arc;
    FaderStyle fader  = FaderStyle::ticks;
    float      corner = 7.0f;   // button corner radius in px; panels scale with it (0..14)
    float      line   = 1.0f;   // stroke weight multiplier (0.75..2)
};

struct FontSpec {
    FontFamily family = FontFamily::system;
    float      scale  = 1.0f;   // 0.9..1.15
};

struct Theme {
    juce::String id, name, author, version { "1.0.0" }, license;
    std::optional<Palette> light, dark;
    Shape shape;
    FontSpec font;
    bool builtIn = false;
    juce::File file;
    bool hasBothModes() const { return light.has_value() && dark.has_value(); }
};

inline constexpr int formatVersion = 1;
inline constexpr juce::int64 maxFileBytes = 16 * 1024;

inline const char* knobName(KnobStyle k)   { return k == KnobStyle::cap ? "cap" : k == KnobStyle::dot ? "dot" : k == KnobStyle::flat ? "flat" : "arc"; }
inline const char* faderName(FaderStyle f) { return f == FaderStyle::plain ? "plain" : "ticks"; }
inline const char* familyName(FontFamily f) { return f == FontFamily::inter ? "inter" : f == FontFamily::mono ? "mono" : "system"; }

inline std::optional<juce::Colour> parseHex(const juce::var& v) {
    if (! v.isString()) return std::nullopt;
    auto s = v.toString().trim();
    if (! s.startsWithChar('#')) return std::nullopt;
    s = s.substring(1);
    if (s.length() != 6 || ! s.containsOnly("0123456789abcdefABCDEF")) return std::nullopt;
    return juce::Colour((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32()));
}
inline juce::String toHex(juce::Colour c) { return "#" + c.toDisplayString(false).toUpperCase(); }

inline bool validId(const juce::String& id) {
    return id.length() >= 3 && id.length() <= 64
        && id.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789._-")
        && id.containsChar('.') && ! id.startsWithChar('.') && ! id.endsWithChar('.');
}

inline std::optional<Palette> parsePalette(const juce::var& v, juce::String& err) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr) return std::nullopt;
    Palette p;
    int found = 0;
    for (int i = 0; i < numTokens; ++i) {
        const juce::Identifier key(tokenNames[i]);
        if (! obj->hasProperty(key)) continue;
        if (auto c = parseHex(obj->getProperty(key))) { p.colour[(size_t) i] = *c; ++found; }
        else { err = juce::String("bad colour for ") + tokenNames[i] + " (use #RRGGBB)"; return std::nullopt; }
    }
    if (obj->hasProperty("shadow")) {
        const auto& sv = obj->getProperty("shadow");
        if (! (sv.isDouble() || sv.isInt() || sv.isInt64())) { err = "shadow must be a number"; return std::nullopt; }
        p.shadow = juce::jlimit(0.0f, 0.6f, (float) (double) sv);
    }
    if (found == 0) { err = "palette has no colours"; return std::nullopt; }
    return p;
}

// Parses and validates a theme file's text. Unknown keys are ignored; numbers are clamped.
inline std::optional<Theme> parse(const juce::String& text, juce::String* error = nullptr) {
    juce::String err;
    auto fail = [&](const juce::String& e) -> std::optional<Theme> { if (error) *error = e; return std::nullopt; };
    if (text.getNumBytesAsUTF8() > (size_t) maxFileBytes) return fail("file is larger than 16 KB");
    juce::var root;
    if (juce::JSON::parse(text, root).failed() || root.getDynamicObject() == nullptr) return fail("not a JSON object");
    const auto get = [&](const char* k) { return root.getProperty(juce::Identifier(k), {}); };

    Theme t;
    const auto fmt = get("format");
    if (! (fmt.isInt() || fmt.isInt64() || fmt.isDouble()) || (int) fmt < 1) return fail("missing format");
    t.id = get("id").toString().trim();
    if (! validId(t.id)) return fail("id must look like author.name (a-z, 0-9, . _ -)");
    t.name = get("name").toString().trim();
    if (t.name.isEmpty() || t.name.length() > 40) return fail("name must be 1 to 40 characters");
    t.author  = get("author").toString().trim().substring(0, 40);
    t.version = get("version").isString() ? get("version").toString().trim().substring(0, 16) : juce::String("1.0.0");
    t.license = get("license").toString().trim().substring(0, 32);

    if (get("light").getDynamicObject() != nullptr) { t.light = parsePalette(get("light"), err); if (! t.light) return fail("light: " + err); }
    if (get("dark").getDynamicObject() != nullptr)  { t.dark  = parsePalette(get("dark"), err);  if (! t.dark)  return fail("dark: " + err); }
    if (! t.light && ! t.dark) return fail("needs a light or a dark palette");

    if (auto* s = get("shape").getDynamicObject()) {
        const auto knob = s->getProperty("knob").toString();
        t.shape.knob = knob == "cap" ? KnobStyle::cap : knob == "dot" ? KnobStyle::dot : knob == "flat" ? KnobStyle::flat : KnobStyle::arc;
        t.shape.fader = s->getProperty("fader").toString() == "plain" ? FaderStyle::plain : FaderStyle::ticks;
        if (s->hasProperty("corner")) t.shape.corner = juce::jlimit(0.0f, 14.0f, (float) (double) s->getProperty("corner"));
        if (s->hasProperty("line"))   t.shape.line   = juce::jlimit(0.75f, 2.0f, (float) (double) s->getProperty("line"));
    }
    if (auto* f = get("font").getDynamicObject()) {
        const auto fam = f->getProperty("family").toString();
        t.font.family = fam == "inter" ? FontFamily::inter : fam == "mono" ? FontFamily::mono : FontFamily::system;
        if (f->hasProperty("scale")) t.font.scale = juce::jlimit(0.9f, 1.15f, (float) (double) f->getProperty("scale"));
    }
    return t;
}

inline juce::var paletteToVar(const Palette& p) {
    auto* o = new juce::DynamicObject();
    for (int i = 0; i < numTokens; ++i)
        if (p.colour[(size_t) i]) o->setProperty(tokenNames[i], toHex(*p.colour[(size_t) i]));
    if (p.shadow) o->setProperty("shadow", (double) *p.shadow);
    return juce::var(o);
}

inline juce::String toJson(const Theme& t) {
    auto* o = new juce::DynamicObject();
    o->setProperty("format", formatVersion);
    o->setProperty("id", t.id);
    o->setProperty("name", t.name);
    if (t.author.isNotEmpty())  o->setProperty("author", t.author);
    o->setProperty("version", t.version);
    if (t.license.isNotEmpty()) o->setProperty("license", t.license);
    if (t.light) o->setProperty("light", paletteToVar(*t.light));
    if (t.dark)  o->setProperty("dark", paletteToVar(*t.dark));
    auto* s = new juce::DynamicObject();
    s->setProperty("knob", knobName(t.shape.knob));
    s->setProperty("fader", faderName(t.shape.fader));
    s->setProperty("corner", (double) t.shape.corner);
    s->setProperty("line", (double) t.shape.line);
    o->setProperty("shape", juce::var(s));
    auto* f = new juce::DynamicObject();
    f->setProperty("family", familyName(t.font.family));
    f->setProperty("scale", (double) t.font.scale);
    o->setProperty("font", juce::var(f));
    return juce::JSON::toString(juce::var(o));
}

// WCAG contrast ratio (1..21) between two colours.
inline double contrast(juce::Colour a, juce::Colour b) {
    auto lum = [](juce::Colour c) {
        auto ch = [](float v) { return v <= 0.03928f ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch(c.getFloatRed()) + 0.7152 * ch(c.getFloatGreen()) + 0.0722 * ch(c.getFloatBlue());
    };
    const double l1 = lum(a), l2 = lum(b);
    return (juce::jmax(l1, l2) + 0.05) / (juce::jmin(l1, l2) + 0.05);
}

//==============================================================================
// Built-in themes (compiled in, so a plugin always draws even with no askp folder)
inline Palette makePalette(std::initializer_list<juce::uint32> argb, float shadow) {
    Palette p;
    int i = 0;
    for (auto c : argb) p.colour[(size_t) i++] = juce::Colour(c);
    p.shadow = shadow;
    return p;
}

//                       bg          footer      panel       surface     line        grid        track       tick        rim         ink         muted       ghost       onInk       mute        solo        alert       accent
inline Palette studioLight() { return makePalette({ 0xffececec, 0xffe1e1e1, 0xfff7f7f7, 0xfffbfbfb, 0xffdcdcdc, 0xffe6e6e6, 0xffe2e2e2, 0xffc4c4c4, 0xffd2d2d2, 0xff1b1b1b, 0xff6b6b6b, 0xff9a9a9a, 0xffffffff, 0xffd9822b, 0xffd8b52a, 0xffe0433a, 0xff1b1b1b }, 0.06f); }
inline Palette studioDark()  { return makePalette({ 0xff1b1c1e, 0xff151618, 0xff25262a, 0xff2b2c30, 0xff393a3f, 0xff303135, 0xff3a3b40, 0xff56575d, 0xff4a4b51, 0xffe9e9ea, 0xff9c9da2, 0xff6f7076, 0xff1e1f22, 0xffd9822b, 0xffd8b52a, 0xffe0433a, 0xffe9e9ea }, 0.30f); }

inline std::vector<Theme> builtIns() {
    std::vector<Theme> v;
    auto add = [&](const char* id, const char* name, std::optional<Palette> l, std::optional<Palette> d,
                   KnobStyle k, FaderStyle f, float corner, float line, FontFamily fam) {
        Theme t; t.id = id; t.name = name; t.author = "askp"; t.license = "CC0-1.0"; t.builtIn = true;
        t.light = l; t.dark = d; t.shape = { k, f, corner, line }; t.font = { fam, 1.0f };
        v.push_back(t);
    };
    add("askp.studio", "Studio", studioLight(), studioDark(), KnobStyle::arc, FaderStyle::ticks, 7.0f, 1.0f, FontFamily::system);
    add("askp.paper", "Paper",
        makePalette({ 0xffece7df, 0xffe2dcd2, 0xfff7f3ec, 0xfffbf8f3, 0xffddd5c9, 0xffe7e0d5, 0xffe3dcd0, 0xffc7beb0, 0xffd3cabc, 0xff2b2620, 0xff6f665b, 0xff9c9385, 0xfffffdf9, 0xffd9822b, 0xffd8b52a, 0xffd4473a, 0xffb5532a }, 0.07f),
        makePalette({ 0xff1e1b17, 0xff181512, 0xff28241f, 0xff2e2a24, 0xff3d372f, 0xff332e28, 0xff3e3830, 0xff5a5247, 0xff4d463c, 0xffefe7da, 0xffa69c8e, 0xff776e62, 0xff221e1a, 0xffe0904a, 0xffdcbc45, 0xffe5594b, 0xffe07f4f }, 0.30f),
        KnobStyle::dot, FaderStyle::ticks, 9.0f, 1.0f, FontFamily::inter);
    add("askp.graphite", "Graphite",
        makePalette({ 0xffe6e9ed, 0xffdce0e5, 0xfff3f5f7, 0xfff8f9fb, 0xffd3d8de, 0xffe0e4e9, 0xffdce1e7, 0xffbcc3cc, 0xffcbd1d9, 0xff1a1f26, 0xff626b77, 0xff949ca7, 0xffffffff, 0xffd9822b, 0xffd8b52a, 0xffe0433a, 0xff2f6fdb }, 0.06f),
        makePalette({ 0xff15181d, 0xff101317, 0xff1e2228, 0xff23282f, 0xff323841, 0xff2a2f37, 0xff343a44, 0xff4e5662, 0xff444b56, 0xffe6eaf0, 0xff98a1ad, 0xff69717c, 0xff191c21, 0xffe0904a, 0xffdcbc45, 0xffe5594b, 0xff5b95f5 }, 0.32f),
        KnobStyle::flat, FaderStyle::plain, 5.0f, 1.0f, FontFamily::inter);
    add("askp.moss", "Moss",
        makePalette({ 0xffe7eae3, 0xffdde1d8, 0xfff4f6f1, 0xfff9faf7, 0xffd5dbce, 0xffe2e6dc, 0xffdde2d6, 0xffbfc7b5, 0xffcdd4c4, 0xff1e241b, 0xff646d5d, 0xff959d8b, 0xffffffff, 0xffd9822b, 0xffd8b52a, 0xffe0433a, 0xff4b8a3c }, 0.06f),
        makePalette({ 0xff171a15, 0xff121410, 0xff20241d, 0xff252a22, 0xff343a2f, 0xff2b3027, 0xff363d31, 0xff505947, 0xff454d3d, 0xffe5ebdf, 0xff9ca592, 0xff6c7464, 0xff1b1e18, 0xffe0904a, 0xffdcbc45, 0xffe5594b, 0xff7cc067 }, 0.30f),
        KnobStyle::cap, FaderStyle::ticks, 8.0f, 1.0f, FontFamily::inter);
    add("askp.console", "Console", std::nullopt,
        makePalette({ 0xff0f0f10, 0xff0a0a0b, 0xff19191b, 0xff1e1e21, 0xff2e2e33, 0xff252529, 0xff2f2f34, 0xff4a4a52, 0xff3e3e45, 0xfff2f2f3, 0xffa0a0a8, 0xff6e6e76, 0xff141416, 0xffe0904a, 0xffdcbc45, 0xffe5594b, 0xfff2a93b }, 0.40f),
        KnobStyle::arc, FaderStyle::plain, 3.0f, 1.25f, FontFamily::inter);
    return v;
}

} // namespace theme

//==============================================================================
// Engine state (one per plugin binary; all instances of that plugin share it)
struct LookState {
    std::vector<theme::Theme> themes;
    juce::String activeId { "askp.studio" };
    bool dark = false;
    bool loaded = false;
    bool pinned = false;              // ASKP_THEME / applyTheme(): show this, don't follow the file
    int generation = 0;               // bumps on every visible change
    theme::Shape shape;
    theme::FontSpec font;
    bool activeHasBoth = true;
    juce::Time settingsStamp, folderStamp;
    juce::int64 settingsSize = -1;
    juce::uint32 lastPollMs = 0;
};
inline LookState& lookState() { static LookState s; return s; }

inline juce::File askpFolder() {
    if (auto env = juce::SystemStats::getEnvironmentVariable("ASKP_LOOK_DIR", {}); env.isNotEmpty())
        return juce::File(env);
   #if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support/askp");
   #else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("askp");
   #endif
}
inline juce::File lookSettingsFile() { return askpFolder().getChildFile("Look.settings"); }
inline juce::File themesFolder()     { return askpFolder().getChildFile("Themes"); }

// The per-plugin UI preferences (window size etc.), kept next to the presets. Before themes this
// also held "darkMode"; that value is read once to seed the shared setting.
inline juce::PropertiesFile::Options uiSettingsOptions() {
    juce::PropertiesFile::Options o;
    o.applicationName = "UI"; o.filenameSuffix = "settings"; o.folderName = ASKP_LOOK_LEGACY_FOLDER;
    o.osxLibrarySubFolder = "Application Support";
    return o;
}
inline juce::PropertiesFile& uiSettings() {
    static juce::PropertiesFile file(uiSettingsOptions());
    return file;
}

inline const theme::Theme* findTheme(const juce::String& id) {
    for (auto& t : lookState().themes) if (t.id == id) return &t;
    return nullptr;
}
inline const std::vector<theme::Theme>& themes() { return lookState().themes; }
inline const theme::Theme& activeTheme() {
    if (auto* t = findTheme(lookState().activeId)) return *t;
    return lookState().themes.front();
}
inline int themeGeneration() { return lookState().generation; }
inline bool darkTheme() { return lookState().dark; }
inline bool activeThemeHasBothModes() { return lookState().activeHasBoth; }
inline const theme::Shape& lookShape() { return lookState().shape; }

// Scale a design-time corner radius / stroke width by the active theme.
inline float corner(float base) { return base * lookShape().corner / 7.0f; }
inline float stroke(float base) { return base * lookShape().line; }

inline void scanThemes() {
    auto& s = lookState();
    s.themes = theme::builtIns();
    const auto dir = themesFolder();
    s.folderStamp = dir.getLastModificationTime();
    if (! dir.isDirectory()) return;
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.askptheme");
    files.sort();
    for (auto& f : files) {
        if (f.getSize() > theme::maxFileBytes) continue;
        if (auto t = theme::parse(f.loadFileAsString())) {
            if (t->id.startsWith("askp.") || findTheme(t->id) != nullptr) continue;   // built-ins and duplicates win
            t->file = f;
            s.themes.push_back(*t);
        }
    }
}

// Copies the active theme into the colour tokens, shape and font. Bumps the generation.
inline void applyActive() {
    auto& s = lookState();
    const auto& t = activeTheme();
    s.activeHasBoth = t.hasBothModes();
    const bool useDark = t.hasBothModes() ? s.dark : t.dark.has_value();
    const auto& pal = useDark ? *t.dark : *t.light;
    const auto base = useDark ? theme::studioDark() : theme::studioLight();
    for (int i = 0; i < theme::numTokens; ++i) {
        const auto& c = pal.colour[(size_t) i];
        *theme::tokenTarget(i) = c ? *c : *base.colour[(size_t) i];
    }
    if (! pal.colour[16]) colours::accent = colours::ink;      // no accent given: monochrome
    colours::shadow = pal.shadow.value_or(*base.shadow);
    s.shape = t.shape;
    s.font = t.font;
    ++s.generation;
}

inline void writeLookSettings() {
    auto& s = lookState();
    auto* o = new juce::DynamicObject();
    o->setProperty("theme", s.activeId);
    o->setProperty("dark", s.dark);
    juce::var old;
    const auto f = lookSettingsFile();
    if (f.existsAsFile()) old = juce::JSON::parse(f.loadFileAsString());
    o->setProperty("counter", (int) old.getProperty("counter", 0) + 1);
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
    s.settingsStamp = f.getLastModificationTime();
    s.settingsSize = f.getSize();
}

// Reads Look.settings. Returns true if theme or mode changed.
inline bool readLookSettings() {
    auto& s = lookState();
    const auto f = lookSettingsFile();
    s.settingsStamp = f.getLastModificationTime();
    s.settingsSize = f.getSize();
    const auto v = juce::JSON::parse(f.loadFileAsString());
    if (v.getDynamicObject() == nullptr) return false;
    const auto id = v.getProperty("theme", "askp.studio").toString();
    const bool dark = (bool) v.getProperty("dark", false);
    const bool changed = id != s.activeId || dark != s.dark;
    s.activeId = id; s.dark = dark;
    return changed;
}

inline void ensureLookLoaded() {
    auto& s = lookState();
    if (s.loaded) return;
    s.loaded = true;
    scanThemes();
    if (lookSettingsFile().existsAsFile()) {
        readLookSettings();
    } else {
        // First run of a theme-aware askp product: carry over this plugin's old light/dark choice.
        const auto legacy = uiSettingsOptions().getDefaultFile();
        if (juce::String(ASKP_LOOK_LEGACY_FOLDER).isNotEmpty() && legacy.existsAsFile())
            s.dark = juce::PropertiesFile(legacy, uiSettingsOptions()).getBoolValue("darkMode", false);
        writeLookSettings();
    }
    if (auto forced = juce::SystemStats::getEnvironmentVariable("ASKP_THEME", {}); forced.isNotEmpty()) {
        s.activeId = forced; s.pinned = true;
    }
    if (findTheme(s.activeId) == nullptr) s.activeId = "askp.studio";
    applyActive();
}

// Checks whether another window, plugin or the Hub changed the theme. Cheap; call from a timer.
inline void pollThemeFile() {
    ensureLookLoaded();
    auto& s = lookState();
    const auto now = juce::Time::getMillisecondCounter();
    if (now - s.lastPollMs < 700) return;
    s.lastPollMs = now;
    if (s.pinned) return;
    bool rescanned = false;
    if (themesFolder().getLastModificationTime() != s.folderStamp) {
        scanThemes(); rescanned = true;
    }
    const auto f = lookSettingsFile();
    const bool fileChanged = f.getLastModificationTime() != s.settingsStamp || f.getSize() != s.settingsSize;
    bool changed = fileChanged && readLookSettings();
    if (findTheme(s.activeId) == nullptr) { s.activeId = "askp.studio"; changed = true; }
    if (changed || rescanned) applyActive();
}

// Choose a theme everywhere (saved for every askp plugin, instance and project).
inline void selectTheme(const juce::String& id) {
    ensureLookLoaded();
    auto& s = lookState();
    if (findTheme(id) == nullptr) return;
    s.activeId = id; s.pinned = false;
    writeLookSettings();
    applyActive();
}

// Light/dark for the current theme, saved everywhere.
inline void saveThemePreference(bool dark) {
    ensureLookLoaded();
    auto& s = lookState();
    s.dark = dark; s.pinned = false;
    writeLookSettings();
    applyActive();
}
// Show light or dark in this process only, without saving (snapshots, tests).
inline void applyTheme(bool dark) {
    ensureLookLoaded();
    lookState().dark = dark; lookState().pinned = true;
    applyActive();
}
// Show a theme in this process only, without saving (snapshots, previews).
inline void previewTheme(const juce::String& id, bool dark) {
    ensureLookLoaded();
    auto& s = lookState();
    if (findTheme(id) != nullptr) s.activeId = id;
    s.dark = dark; s.pinned = true;
    applyActive();
}
inline void loadThemePreference() { ensureLookLoaded(); }

//==============================================================================
// Fonts. Inter (SIL Open Font License, Latin subset) is compiled in by look/AskpFonts.cpp.
namespace fonts {
    extern const unsigned char interRegular[]; extern const int interRegularSize;
    extern const unsigned char interBold[];    extern const int interBoldSize;

    struct Inter final : public juce::DeletedAtShutdown {
        juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor(interRegular, (size_t) interRegularSize);
        juce::Typeface::Ptr bold    = juce::Typeface::createSystemTypefaceFor(interBold, (size_t) interBoldSize);
        ~Inter() override { clearSingletonInstance(); }
        JUCE_DECLARE_SINGLETON_SINGLETHREADED_MINIMAL_INLINE(Inter)
    };
}

inline juce::Font themedFont(float size, bool bold) {
    const auto& f = lookState().font;
    size *= f.scale;
    if (f.family == FontFamily::inter)
        if (auto* inter = fonts::Inter::getInstance())
            if (auto tf = bold ? inter->bold : inter->regular)
                return juce::Font(juce::FontOptions(tf).withHeight(size));
    if (f.family == FontFamily::mono)
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font labelFont(float size = 10.0f) { return themedFont(size, true).withExtraKerningFactor(0.12f); }
inline juce::Font bodyFont(float size = 12.0f, bool bold = false) { return themedFont(size, bold); }

//==============================================================================
// Rounded panel with a hairline border and a soft shadow.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.0f) {
    radius = corner(radius);
    juce::DropShadow(juce::Colours::black.withAlpha(colours::shadow), 6, { 0, 2 }).drawForPath(g, [&] {
        juce::Path p; p.addRoundedRectangle(r, radius); return p; }());
    g.setColour(colours::panel);
    g.fillRoundedRectangle(r, radius);
    g.setColour(colours::line);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

// Draws a knob in the active theme's style. Usable from custom components too.
inline void drawKnob(juce::Graphics& g, juce::Rectangle<float> bounds, float pos, float start, float end, bool enabled = true) {
    using namespace colours;
    const auto r = bounds.reduced(3.0f);
    const float size = juce::jmin(r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const float radius = size * 0.5f - 2.0f;
    const float angle = start + pos * (end - start);
    const auto valueCol = enabled ? accent : ghost;
    const auto round = juce::PathStrokeType::rounded;
    auto arcPath = [&](float rad, float a0, float a1) { juce::Path p; p.addCentredArc(c.x, c.y, rad, rad, 0.0f, a0, a1, true); return p; };

    switch (lookShape().knob) {
        case KnobStyle::cap: {          // dark cap like hardware, value ring outside
            g.setColour(track);
            g.strokePath(arcPath(radius, start, end), juce::PathStrokeType(stroke(2.0f), juce::PathStrokeType::curved, round));
            g.setColour(valueCol);
            g.strokePath(arcPath(radius, start, angle), juce::PathStrokeType(stroke(2.5f), juce::PathStrokeType::curved, round));
            const float cap = radius * 0.74f;
            g.setColour(enabled ? ink : ghost);
            g.fillEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c));
            juce::Path p; p.startNewSubPath(c.getPointOnCircumference(cap * 0.25f, angle)); p.lineTo(c.getPointOnCircumference(cap * 0.8f, angle));
            g.setColour(white);
            g.strokePath(p, juce::PathStrokeType(stroke(2.0f), juce::PathStrokeType::curved, round));
            break;
        }
        case KnobStyle::dot: {          // light cap with an accent dot, full track behind the value
            g.setColour(track);
            g.strokePath(arcPath(radius, start, end), juce::PathStrokeType(stroke(2.0f), juce::PathStrokeType::curved, round));
            g.setColour(valueCol);
            g.strokePath(arcPath(radius, start, angle), juce::PathStrokeType(stroke(2.0f), juce::PathStrokeType::curved, round));
            const float cap = radius * 0.72f;
            g.setColour(white);
            g.fillEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c));
            g.setColour(rim);
            g.drawEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c), stroke(1.0f));
            const float d = juce::jmax(3.0f, cap * 0.22f);
            g.setColour(valueCol);
            g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(c.getPointOnCircumference(cap * 0.66f, angle)));
            break;
        }
        case KnobStyle::flat: {         // no cap: track + value arc and a pointer line
            g.setColour(track);
            g.strokePath(arcPath(radius, start, end), juce::PathStrokeType(stroke(3.0f), juce::PathStrokeType::curved, round));
            g.setColour(valueCol);
            g.strokePath(arcPath(radius, start, angle), juce::PathStrokeType(stroke(3.0f), juce::PathStrokeType::curved, round));
            juce::Path p; p.startNewSubPath(c.getPointOnCircumference(radius * 0.18f, angle)); p.lineTo(c.getPointOnCircumference(radius * 0.72f, angle));
            g.setColour(enabled ? ink : ghost);
            g.strokePath(p, juce::PathStrokeType(stroke(2.0f), juce::PathStrokeType::curved, round));
            break;
        }
        case KnobStyle::arc:
        default: {                      // Studio: thin value arc, white cap, ink pointer
            g.setColour(valueCol);
            g.strokePath(arcPath(radius, start, angle), juce::PathStrokeType(stroke(2.5f), juce::PathStrokeType::curved, round));
            const float cap = radius * 0.72f;
            g.setColour(white);
            g.fillEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c));
            g.setColour(rim);
            g.drawEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c), stroke(1.0f));
            juce::Path pointer;
            pointer.startNewSubPath(c);
            pointer.lineTo(c.getPointOnCircumference(cap * 0.8f, angle));
            g.setColour(enabled ? ink : ghost);
            g.strokePath(pointer, juce::PathStrokeType(stroke(2.0f), juce::PathStrokeType::curved, round));
            break;
        }
    }
}

//==============================================================================
class StudioLook final : public juce::LookAndFeel_V4 {
public:
    StudioLook() { loadThemePreference(); refreshColours(); }

    // Re-read the palette into JUCE's colour ids (after the theme changes).
    void refreshColours() {
        using namespace colours;
        setColour(juce::ResizableWindow::backgroundColourId, bg);
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::backgroundColourId, white);
        setColour(juce::ComboBox::outlineColourId, line);
        setColour(juce::ComboBox::textColourId, ink);
        setColour(juce::ComboBox::arrowColourId, ink);
        setColour(juce::PopupMenu::backgroundColourId, white);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::headerTextColourId, muted);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, ink);
        setColour(juce::PopupMenu::highlightedTextColourId, white);
        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, ink);
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, white);
        setColour(juce::TooltipWindow::backgroundColourId, panel);
        setColour(juce::TooltipWindow::textColourId, ink);
        setColour(juce::TooltipWindow::outlineColourId, line);
        setColour(juce::ScrollBar::thumbColourId, tick);
        setColour(juce::Label::textColourId, ink);
        setColour(juce::TextEditor::backgroundColourId, white);
        setColour(juce::TextEditor::textColourId, ink);
        setColour(juce::TextEditor::outlineColourId, line);
        setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.25f));
        setColour(juce::CaretComponent::caretColourId, ink);
        setColour(juce::AlertWindow::backgroundColourId, panel);
        setColour(juce::AlertWindow::textColourId, ink);
        setColour(juce::AlertWindow::outlineColourId, line);
        shownGeneration = themeGeneration();
    }
    int shownGeneration = -1;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float start, float end, juce::Slider& s) override {
        drawKnob(g, juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h), pos, start, end, s.isEnabled());
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& s) override {
        if (style == juce::Slider::LinearHorizontal) {   // thin track, accent fill, round thumb
            const float cy = (float) y + (float) h * 0.5f;
            const auto tr = juce::Rectangle<float>((float) x, cy - 2.0f, (float) w, 4.0f);
            g.setColour(colours::track);
            g.fillRoundedRectangle(tr, 2.0f);
            g.setColour(s.isEnabled() ? colours::accent : colours::ghost);
            g.fillRoundedRectangle(tr.withRight(sliderPos), 2.0f);
            g.setColour(s.isEnabled() ? colours::ink : colours::ghost);
            g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ sliderPos, cy }));
            return;
        }
        if (style != juce::Slider::LinearVertical) {
            LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, 0, 0, style, s);
            return;
        }
        const bool plain = lookShape().fader == FaderStyle::plain;
        const float cx = (float) x + (float) w * 0.5f - (plain ? 0.0f : 5.0f);
        const auto trackR = juce::Rectangle<float>(cx - 3.0f, (float) y, 6.0f, (float) h);
        g.setColour(colours::track);
        g.fillRoundedRectangle(trackR, 3.0f);
        g.setColour(s.isEnabled() ? colours::accent : colours::ghost);
        g.fillRoundedRectangle(trackR.withTop(sliderPos), 3.0f);
        if (! plain) {                       // tick marks
            g.setColour(colours::tick);
            for (float ty = (float) y; ty <= (float) (y + h); ty += 8.0f)
                g.fillRect(cx + 7.0f, ty, 6.0f, 1.0f);
        }
        g.setColour(colours::ink);           // handle
        g.fillRoundedRectangle(juce::Rectangle<float>(plain ? 20.0f : 16.0f, 6.0f).withCentre({ cx, sliderPos }), juce::jmin(3.0f, corner(3.0f) + 0.5f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = b.getToggleState();
        g.setColour(on ? colours::ink : (down ? colours::track : (over ? colours::white : colours::panel)));
        g.fillRoundedRectangle(r, corner(7.0f));
        if (! on) { g.setColour(colours::line); g.drawRoundedRectangle(r, corner(7.0f), 1.0f); }
    }

    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return bodyFont(juce::jmin(12.0f, (float) h * 0.5f), true); }
    juce::Font getLabelFont(juce::Label& l) override {
        const auto f = l.getFont();
        return lookState().font.family == FontFamily::system ? f : themedFont(f.getHeight(), f.isBold());
    }
    juce::Font getPopupMenuFont() override { return bodyFont(13.0f); }
    juce::Font getAlertWindowMessageFont() override { return bodyFont(13.0f); }
    juce::Font getSliderPopupFont(juce::Slider&) override { return bodyFont(12.0f, true); }

    // Toggle buttons are drawn as small pill switches with their label on the left.
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool) override {
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        auto sw = r.removeFromRight(28.0f).withSizeKeepingCentre(28.0f, 16.0f);
        g.setColour(on ? colours::accent : colours::rim);
        g.fillRoundedRectangle(sw, 8.0f);
        g.setColour(colours::white);
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ on ? sw.getRight() - 8.0f : sw.getX() + 8.0f, sw.getCentreY() }));
        g.setColour(b.isEnabled() ? colours::ink : colours::muted);
        g.setFont(bodyFont(11.0f));
        g.drawText(b.getButtonText(), r, juce::Justification::centredLeft);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override {
        const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
        g.setColour(colours::white);
        g.fillRoundedRectangle(r, corner(7.0f));
        g.setColour(colours::line);
        g.drawRoundedRectangle(r, corner(7.0f), 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.startNewSubPath(ax - 4, ay - 2); arrow.lineTo(ax, ay + 2); arrow.lineTo(ax + 4, ay - 2);
        g.setColour(colours::ink);
        g.strokePath(arrow, juce::PathStrokeType(1.5f));
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return bodyFont(12.0f, true); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(8, 0, box.getWidth() - 26, box.getHeight());
        label.setFont(getComboBoxFont(box));
    }
};

//==============================================================================
// Header button that flips light/dark (its owner calls saveThemePreference): a circle, half
// filled. Dimmed when the active theme has only one palette, where the flip shows no change.
class ThemeButton final : public juce::Button {
public:
    ThemeButton() : juce::Button("Theme") { setTooltip("Light or dark (saved for every askp plugin)"); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const bool live = activeThemeHasBothModes();
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down && live ? colours::track : (over && live ? colours::white : colours::panel));
        g.fillRoundedRectangle(r, corner(6.0f));
        g.setColour(colours::line);
        g.drawRoundedRectangle(r, corner(6.0f), 1.0f);
        const auto c = r.withSizeKeepingCentre(14.0f, 14.0f);
        g.setColour(live ? colours::ink : colours::ghost);
        g.drawEllipse(c, 1.5f);
        juce::Path half;
        half.addPieSegment(c, 0.0f, juce::MathConstants<float>::pi, 0.0f);
        g.fillPath(half);
    }
};

// Opens the theme menu: every installed theme, light/dark, and the themes folder.
inline void showThemeMenu(juce::Component& target, std::function<void()> afterChange = {}) {
    ensureLookLoaded();
    scanThemes();                                          // pick up files added since the last poll
    juce::PopupMenu m;
    m.addSectionHeader("Theme (all askp plugins)");
    const auto& list = themes();
    for (int i = 0; i < (int) list.size(); ++i)
        m.addItem(100 + i, list[(size_t) i].name, true, list[(size_t) i].id == activeTheme().id);
    m.addSeparator();
    m.addItem(1, "Dark mode", activeThemeHasBothModes(), darkTheme());
    m.addItem(2, "Open themes folder");
    juce::Component::SafePointer<juce::Component> safe(&target);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target),
        [safe, afterChange, ids = [&] { juce::StringArray a; for (auto& t : list) a.add(t.id); return a; }()](int r) {
            if (r == 1) saveThemePreference(! darkTheme());
            else if (r == 2) { themesFolder().createDirectory(); themesFolder().startAsProcess(); return; }
            else if (r >= 100 && r - 100 < ids.size()) selectTheme(ids[r - 100]);
            else return;
            if (safe != nullptr && afterChange) afterChange();
        });
}

// Header button that opens the theme menu: three swatches of the current theme.
class ThemeMenuButton final : public juce::Button {
public:
    ThemeMenuButton() : juce::Button("Themes") {
        setTooltip("Theme for every askp plugin");
        onClick = [this] { showThemeMenu(*this, onThemeChanged); };
    }
    std::function<void()> onThemeChanged;   // called after a pick, in this window
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? colours::track : (over ? colours::white : colours::panel));
        g.fillRoundedRectangle(r, corner(6.0f));
        g.setColour(colours::line);
        g.drawRoundedRectangle(r, corner(6.0f), 1.0f);
        const auto c = r.getCentre();
        const float d = 8.0f;
        const juce::Colour dots[3] = { colours::accent, colours::ink, colours::rim };
        for (int i = 0; i < 3; ++i) {
            const auto p = c.translated(((float) i - 1.0f) * 5.5f, 0.0f);
            g.setColour(dots[i]);
            g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(p));
            g.setColour(colours::panel);
            g.drawEllipse(juce::Rectangle<float>(d, d).withCentre(p), 1.0f);
        }
    }
};

// Keeps one window in step with the shared theme: polls the settings file and, when the theme
// changes (here, in another window, another plugin or the Hub), refreshes the look and repaints.
class ThemeFollower final : private juce::Timer {
public:
    // look: StudioLook, or any LookAndFeel with a refreshColours() method
    template <class Look>
    ThemeFollower(juce::Component& c, Look& l, std::function<void()> changed = {})
        : comp(c), refreshLook([&l] { l.refreshColours(); }), onChange(std::move(changed)) { shown = themeGeneration(); startTimer(250); }
    void check() { timerCallback(); }
private:
    void timerCallback() override {
        pollThemeFile();
        if (shown == themeGeneration()) return;
        shown = themeGeneration();
        refreshLook();
        comp.sendLookAndFeelChange();
        if (onChange) onChange();
        comp.repaint();
    }
    juce::Component& comp;
    std::function<void()> refreshLook, onChange;
    int shown = -1;
};

} // namespace ASKP_LOOK_NS
