#pragma once
// Puts plugins on disk and takes them off again. No UI in here: the app and
// the tests both drive it, and every long call reports progress through a
// callback that can also cancel.
#include "Manifest.h"
#include <functional>
#include <map>

namespace hub {

struct Paths {
    juce::File dataDir;                          // state file, download cache, staging
    std::map<Format, juce::File> pluginDirs;     // where the Hub installs each format
    std::map<Format, juce::File> systemDirs;     // other places a copy may already live (read only)

    // The real locations for this computer. ASKP_HUB_ROOT=<folder> swaps all of
    // them for folders under <folder>, for tests and dry runs.
    static Paths defaults();
    static Paths sandbox(const juce::File& root);
};

struct InstallStatus {
    bool installed = false;
    bool managed = false;                 // installed by the Hub (we know its version and files)
    juce::String version;                 // empty when a copy was found but its version can't be read
    std::vector<Format> formats;          // formats present in the Hub's folders
    juce::StringArray elsewhere;          // copies found in system folders, e.g. an old .pkg install
};

class Installer {
public:
    // fraction 0..1 (or -1 when unknown) and a short status line; return false to cancel.
    using Progress = std::function<bool(double fraction, const juce::String& status)>;

    explicit Installer(Paths p) : paths(std::move(p)) {}

    const Paths& getPaths() const { return paths; }

    InstallStatus status(const PluginInfo&) const;

    // Downloads, verifies and installs `formats` of the plugin, replacing any
    // installed copy. Formats installed before but not listed are removed, so this
    // also applies a changed format choice. Returns an error message, empty on success.
    juce::String install(const PluginInfo&, const std::vector<Format>& formats, const Progress& = {});

    // Removes every format the Hub installed (and any copy left in the Hub's folders).
    juce::String uninstall(const PluginInfo&);

    // Writes text to the cache next to the state file (used for the last good manifest).
    void writeCache(const juce::String& name, const juce::String& text) const;
    juce::String readCache(const juce::String& name) const;

private:
    Paths paths;
    juce::File stateFile() const { return paths.dataDir.getChildFile("installed.json"); }
    juce::var loadState() const;
    void saveState(const juce::var&) const;
    juce::String download(const Download&, const juce::File& dest, const Progress&) const;
    static juce::String readBundleVersion(const juce::File& bundle);
    static void afterInstall(const juce::File& bundle, Format);
};

} // namespace hub
