#pragma once
// The catalogue the Hub reads: one JSON file (manifest.json) listing every
// plugin, its latest version and where to download it for each platform.
// See docs/DESIGN.md for the schema.
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace hub {

// Plugin formats the Hub knows how to place on disk.
enum class Format { vst3, au, clap };

inline const std::vector<Format>& allFormats() {
    static const std::vector<Format> f { Format::vst3, Format::au, Format::clap };
    return f;
}
inline juce::String formatKey(Format f) {   // manifest key, state file key
    switch (f) { case Format::vst3: return "vst3"; case Format::au: return "au"; case Format::clap: return "clap"; }
    return {};
}
inline juce::String formatLabel(Format f) {   // shown in the UI
    switch (f) { case Format::vst3: return "VST3"; case Format::au: return "AU"; case Format::clap: return "CLAP"; }
    return {};
}
// Folder inside a plugin zip that holds this format's bundle. Same names as
// JUCE's <Target>_artefacts/Release folders, so a zip is just those folders.
inline juce::String zipFolder(Format f) {
    switch (f) { case Format::vst3: return "VST3"; case Format::au: return "AU"; case Format::clap: return "CLAP"; }
    return {};
}
inline std::optional<Format> formatFromKey(const juce::String& k) {
    for (auto f : allFormats()) if (formatKey(f) == k) return f;
    return std::nullopt;
}

// "mac", "win" or "linux": the key used for downloads in the manifest.
inline juce::String platformKey() {
   #if JUCE_MAC
    return "mac";
   #elif JUCE_WINDOWS
    return "win";
   #else
    return "linux";
   #endif
}

struct Download {
    juce::String url, sha256;
    juce::int64 size = 0;
    std::vector<Format> formats;   // what this zip holds; empty means every bundle listed
    bool valid() const { return url.isNotEmpty(); }
};

struct PluginInfo {
    juce::String id, name, badge, tagline, version, released, notes, homepage;
    std::vector<std::pair<Format, juce::String>> bundles;   // format -> bundle file name, e.g. VA.vst3
    std::map<juce::String, Download> downloads;            // platform key -> download

    const Download* download() const {
        auto it = downloads.find(platformKey());
        return it != downloads.end() && it->second.valid() ? &it->second : nullptr;
    }
    juce::String bundleName(Format f) const {
        for (auto& [fmt, file] : bundles) if (fmt == f) return file;
        return {};
    }
    // Formats this plugin ships that this platform can use (no AU outside macOS),
    // narrowed to what this platform's zip holds when the manifest says.
    std::vector<Format> availableFormats() const {
        std::vector<Format> out;
        const auto* dl = download();
        for (auto& [fmt, file] : bundles) {
            juce::ignoreUnused(file);
           #if ! JUCE_MAC
            if (fmt == Format::au) continue;
           #endif
            if (dl != nullptr && ! dl->formats.empty() && std::find(dl->formats.begin(), dl->formats.end(), fmt) == dl->formats.end())
                continue;
            out.push_back(fmt);
        }
        return out;
    }
};

struct Manifest {
    static constexpr int supportedSchema = 1;
    int schema = 0;
    juce::String hubVersion, hubNotes;
    std::map<juce::String, juce::String> hubDownloads;   // platform key -> URL of the Hub installer
    std::vector<PluginInfo> plugins;

    const PluginInfo* find(const juce::String& id) const {
        for (auto& p : plugins) if (p.id == id) return &p;
        return nullptr;
    }

    static std::optional<Manifest> parse(const juce::String& text, juce::String& error) {
        juce::var root;
        if (const auto r = juce::JSON::parse(text, root); r.failed() || ! root.isObject()) {
            error = "The plugin list is not valid JSON" + (r.failed() ? (": " + r.getErrorMessage()) : juce::String());
            return std::nullopt;
        }
        Manifest m;
        m.schema = (int) root.getProperty("schema", 0);
        if (m.schema < 1 || m.schema > supportedSchema) {
            error = m.schema > supportedSchema ? "This plugin list needs a newer Hub. Please update the Hub."
                                               : "The plugin list has no schema version.";
            return std::nullopt;
        }
        if (auto* hub = root.getProperty("hub", {}).getDynamicObject()) {
            m.hubVersion = hub->getProperty("version").toString();
            m.hubNotes = hub->getProperty("notes").toString();
            if (auto* dl = hub->getProperty("download").getDynamicObject())
                for (auto& kv : dl->getProperties()) m.hubDownloads[kv.name.toString()] = kv.value.toString();
        }
        if (auto* list = root.getProperty("plugins", {}).getArray()) {
            for (auto& item : *list) {
                PluginInfo p;
                p.id = item.getProperty("id", {}).toString();
                p.name = item.getProperty("name", {}).toString();
                p.badge = item.getProperty("badge", p.name.substring(0, 3).toUpperCase()).toString();
                p.tagline = item.getProperty("tagline", {}).toString();
                p.version = item.getProperty("version", {}).toString();
                p.released = item.getProperty("released", {}).toString();
                p.notes = item.getProperty("notes", {}).toString();
                p.homepage = item.getProperty("homepage", {}).toString();
                if (auto* b = item.getProperty("bundles", {}).getDynamicObject())
                    for (auto& kv : b->getProperties())
                        if (auto f = formatFromKey(kv.name.toString()))
                            p.bundles.emplace_back(*f, kv.value.toString());
                if (auto* d = item.getProperty("downloads", {}).getDynamicObject())
                    for (auto& kv : d->getProperties()) {
                        Download dl;
                        dl.url = kv.value.getProperty("url", {}).toString();
                        dl.sha256 = kv.value.getProperty("sha256", {}).toString().toLowerCase();
                        dl.size = (juce::int64) kv.value.getProperty("size", 0);
                        if (auto* fl = kv.value.getProperty("formats", {}).getArray())
                            for (auto& fk : *fl)
                                if (auto f = formatFromKey(fk.toString())) dl.formats.push_back(*f);
                        p.downloads[kv.name.toString()] = dl;
                    }
                // A plugin without an id, name, version or bundle can't be installed: skip it
                // rather than failing the whole list.
                if (p.id.isEmpty() || p.name.isEmpty() || p.version.isEmpty() || p.bundles.empty()) continue;
                // bundle names come from the network: never let one climb out of a plugin folder
                bool safe = true;
                for (auto& [f, n] : p.bundles)
                    if (n.isEmpty() || n.containsAnyOf("/\\:") || n.startsWith(".")) safe = false;
                if (safe) m.plugins.push_back(std::move(p));
            }
        }
        return m;
    }
};

// Compares dotted versions numerically: 0.10.0 > 0.9.2. Anything after a '-' is ignored.
inline int compareVersions(const juce::String& a, const juce::String& b) {
    const auto pa = juce::StringArray::fromTokens(a.upToFirstOccurrenceOf("-", false, false).trim(), ".", "");
    const auto pb = juce::StringArray::fromTokens(b.upToFirstOccurrenceOf("-", false, false).trim(), ".", "");
    for (int i = 0; i < juce::jmax(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i].getIntValue() : 0;
        const int y = i < pb.size() ? pb[i].getIntValue() : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

} // namespace hub
