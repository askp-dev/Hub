#include "Installer.h"
#include <juce_cryptography/juce_cryptography.h>

namespace hub {

namespace {
juce::File home() { return juce::File::getSpecialLocation(juce::File::userHomeDirectory); }

// Removes a file or bundle folder. Bundles are folders on macOS (and for VST3/CLAP
// on every platform), single files for some older Windows VST3s.
bool removeItem(const juce::File& f) {
    if (! f.exists() && ! f.isSymbolicLink()) return true;
    return f.isDirectory() && ! f.isSymbolicLink() ? f.deleteRecursively() : f.deleteFile();
}

// Moves a staged bundle into place. rename() when both are on the same volume,
// a copy otherwise.
bool moveItem(const juce::File& from, const juce::File& to) {
    if (from.moveFileTo(to)) return true;
    if (from.isDirectory()) return from.copyDirectoryTo(to) && from.deleteRecursively();
    return from.copyFileTo(to) && from.deleteFile();
}
} // namespace

juce::String Paths::env(const char* name) {
    // Current name first, then the pre-rebrand ASKP_* one.
    auto v = juce::SystemStats::getEnvironmentVariable(juce::String("FUELEDBY_") + name, {});
    return v.isNotEmpty() ? v : juce::SystemStats::getEnvironmentVariable(juce::String("ASKP_") + name, {});
}

Paths Paths::defaults() {
    if (const auto root = env("HUB_ROOT"); root.isNotEmpty())
        return sandbox(juce::File(root));
    Paths p;
   #if JUCE_MAC
    // Per-user folders: no admin password, and every Mac DAW scans them.
    const auto user = home().getChildFile("Library/Audio/Plug-Ins");
    p.pluginDirs = { { Format::vst3, user.getChildFile("VST3") },
                     { Format::au, user.getChildFile("Components") },
                     { Format::clap, user.getChildFile("CLAP") } };
    const juce::File sys("/Library/Audio/Plug-Ins");
    p.systemDirs = { { Format::vst3, sys.getChildFile("VST3") },
                     { Format::au, sys.getChildFile("Components") },
                     { Format::clap, sys.getChildFile("CLAP") } };
    p.dataDir = home().getChildFile("Library/Application Support/fueledby Hub");
    p.legacyDataDir = home().getChildFile("Library/Application Support/askp Hub");
   #elif JUCE_WINDOWS
    // A "fueledby" folder inside the standard shared folders. Hosts scan them recursively.
    // The Hub's setup (run once as administrator) creates these two folders and lets
    // users write to them, so the Hub itself never needs admin rights (packaging/windows/hub.iss).
    const juce::File common(juce::SystemStats::getEnvironmentVariable("CommonProgramFiles", "C:\\Program Files\\Common Files"));
    p.pluginDirs = { { Format::vst3, common.getChildFile("VST3").getChildFile("fueledby") },
                     { Format::clap, common.getChildFile("CLAP").getChildFile("fueledby") } };
    p.systemDirs = { { Format::vst3, common.getChildFile("VST3") }, { Format::clap, common.getChildFile("CLAP") } };
    p.dataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("fueledby Hub");
    // Before the rebrand these were "askp" folders. Only the Setup can make a folder here that users
    // may write to, so if this Hub runs before the new Setup has made the fueledby ones, stay put.
    p.legacyPluginDirs = { { Format::vst3, common.getChildFile("VST3").getChildFile("askp") },
                           { Format::clap, common.getChildFile("CLAP").getChildFile("askp") } };
    for (auto& [format, dir] : p.pluginDirs)
        if (! dir.isDirectory() && p.legacyPluginDirs[format].isDirectory()) dir = p.legacyPluginDirs[format];
    p.legacyDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("askp Hub");
   #else
    p.pluginDirs = { { Format::vst3, home().getChildFile(".vst3") }, { Format::clap, home().getChildFile(".clap") } };
    p.dataDir = home().getChildFile(".config/fueledby Hub");
    p.legacyDataDir = home().getChildFile(".config/askp Hub");
   #endif
    return p;
}

Paths Paths::sandbox(const juce::File& root) {
    Paths p;
    const auto plugins = root.getChildFile("Plug-Ins");
    p.pluginDirs = { { Format::vst3, plugins.getChildFile("VST3") },
                     { Format::au, plugins.getChildFile("Components") },
                     { Format::clap, plugins.getChildFile("CLAP") } };
    const auto sys = root.getChildFile("System");
    p.systemDirs = { { Format::vst3, sys.getChildFile("VST3") }, { Format::au, sys.getChildFile("Components") },
                     { Format::clap, sys.getChildFile("CLAP") } };
    p.dataDir = root.getChildFile("Data");
    return p;
}

// ---------------------------------------------------------------- rebrand

void Installer::migrateLegacy() const {
    // The Hub's own folder (installed.json, cached manifest): move it if the new one isn't there yet.
    if (paths.legacyDataDir != juce::File() && paths.legacyDataDir.isDirectory() && ! paths.dataDir.exists()) {
        paths.dataDir.getParentDirectory().createDirectory();
        moveItem(paths.legacyDataDir, paths.dataDir);
    }
    // Plugins in the old askp folders (Windows): move each bundle into the new folder, then drop the
    // old folder if it is empty. A bundle a DAW has open can't move; it's tried again next launch.
    for (const auto& [format, legacy] : paths.legacyPluginDirs) {
        const auto it = paths.pluginDirs.find(format);
        if (it == paths.pluginDirs.end() || it->second == legacy || ! legacy.isDirectory() || ! it->second.isDirectory())
            continue;
        for (const auto& item : legacy.findChildFiles(juce::File::findFilesAndDirectories, false)) {
            const auto dest = it->second.getChildFile(item.getFileName());
            if (dest.exists()) removeItem(item);      // the newer copy is already in place
            else moveItem(item, dest);
        }
        if (legacy.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) legacy.deleteFile();
    }
}

// ---------------------------------------------------------------- state

juce::var Installer::loadState() const {
    juce::var v = juce::JSON::parse(stateFile());
    if (! v.isObject()) v = juce::var(new juce::DynamicObject());
    if (! v.getProperty("plugins", {}).isObject())
        v.getDynamicObject()->setProperty("plugins", juce::var(new juce::DynamicObject()));
    return v;
}

void Installer::saveState(const juce::var& v) const {
    paths.dataDir.createDirectory();
    const auto tmp = stateFile().getSiblingFile("installed.json.tmp");
    tmp.replaceWithText(juce::JSON::toString(v));
    tmp.moveFileTo(stateFile());   // replace in one step so a crash never leaves half a file
}

void Installer::writeCache(const juce::String& name, const juce::String& text) const {
    paths.dataDir.createDirectory();
    paths.dataDir.getChildFile(name).replaceWithText(text);
}

juce::String Installer::readCache(const juce::String& name) const {
    return paths.dataDir.getChildFile(name).loadFileAsString();
}

// ---------------------------------------------------------------- status

juce::String Installer::readBundleVersion(const juce::File& bundle) {
    // macOS bundles (and JUCE's VST3/CLAP bundles everywhere) carry Contents/Info.plist
    const auto plist = bundle.getChildFile("Contents/Info.plist");
    if (! plist.existsAsFile()) return {};
    auto xml = juce::XmlDocument::parse(plist);
    if (xml == nullptr) return {};
    if (auto* dict = xml->getChildByName("dict"))
        for (auto* e = dict->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->hasTagName("key") && e->getAllSubText() == "CFBundleShortVersionString")
                if (auto* val = e->getNextElement()) return val->getAllSubText().trim();
    return {};
}

InstallStatus Installer::status(const PluginInfo& info) const {
    InstallStatus s;
    const auto entry = loadState()["plugins"][juce::Identifier(info.id)];
    for (auto f : info.availableFormats()) {
        auto it = paths.pluginDirs.find(f);
        if (it == paths.pluginDirs.end()) continue;
        const auto bundle = it->second.getChildFile(info.bundleName(f));
        if (bundle.exists()) {
            s.formats.push_back(f);
            if (s.version.isEmpty()) s.version = readBundleVersion(bundle);
        }
        if (auto sys = paths.systemDirs.find(f); sys != paths.systemDirs.end())
            if (const auto other = sys->second.getChildFile(info.bundleName(f)); other.exists())
                s.elsewhere.add(other.getFullPathName());
    }
    s.installed = ! s.formats.empty();
    if (s.installed && entry.isObject()) {
        s.managed = true;
        s.version = entry["version"].toString();   // trust what we installed over the plist
    }
    return s;
}

// ---------------------------------------------------------------- install

juce::String Installer::download(const Download& dl, const juce::File& dest, const Progress& progress) const {
    const juce::URL url(dl.url);
    int statusCode = 0;
    std::unique_ptr<juce::InputStream> in;
    if (url.isLocalFile())
        in = url.getLocalFile().createInputStream();
    else
        in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                       .withConnectionTimeoutMs(20000)
                                       .withNumRedirectsToFollow(10)   // GitHub release links redirect to a CDN
                                       .withStatusCode(&statusCode)
                                       .withExtraHeaders("User-Agent: fueledby-hub\r\n"));
    if (in == nullptr || statusCode >= 400)
        return "Couldn't reach the download" + (statusCode >= 400 ? " (HTTP " + juce::String(statusCode) + ")" : juce::String())
               + ". Check your internet connection and try again.";

    const auto part = dest.getSiblingFile(dest.getFileName() + ".part");
    part.deleteFile();
    {
        juce::FileOutputStream out(part);
        if (! out.openedOk()) return "Couldn't write to " + part.getParentDirectory().getFullPathName();
        const juce::int64 total = in->getTotalLength() > 0 ? in->getTotalLength() : dl.size;
        juce::HeapBlock<char> buf(1 << 16);
        juce::int64 done = 0;
        for (;;) {
            const int n = in->read(buf, 1 << 16);
            if (n <= 0) break;
            if (! out.write(buf, (size_t) n)) return "The disk is full or not writable.";
            done += n;
            if (progress && ! progress(total > 0 ? 0.8 * (double) done / (double) total : -1.0,
                                       "Downloading " + juce::File::descriptionOfSizeInBytes(done)
                                           + (total > 0 ? " of " + juce::File::descriptionOfSizeInBytes(total) : juce::String()))) {
                out.flush();
                part.deleteFile();
                return "Cancelled";
            }
        }
        out.flush();
        if (out.getStatus().failed()) return "The disk is full or not writable.";
    }
    if (dl.size > 0 && part.getSize() != dl.size) {
        part.deleteFile();
        return "The download was incomplete. Please try again.";
    }
    if (dl.sha256.isNotEmpty()) {
        if (progress) progress(0.82, "Checking the download");
        if (juce::SHA256(part).toHexString().toLowerCase() != dl.sha256) {
            part.deleteFile();
            return "The download didn't match its checksum, so nothing was installed. Please try again.";
        }
    }
    dest.deleteFile();
    return part.moveFileTo(dest) ? juce::String() : "Couldn't save the download.";
}

void Installer::afterInstall(const juce::File& bundle, Format f) {
   #if JUCE_MAC
    // Files the Hub writes aren't normally quarantined, but a plugin zip copied in
    // by hand may be: clear the flag so DAWs load it without a Gatekeeper prompt.
    juce::ChildProcess xattr;
    if (xattr.start(juce::StringArray { "/usr/bin/xattr", "-dr", "com.apple.quarantine", bundle.getFullPathName() }))
        xattr.waitForProcessToFinish(10000);
    if (f == Format::au) {   // make Logic and auval see the new Audio Unit without a reboot
        juce::ChildProcess k;
        if (k.start(juce::StringArray { "/usr/bin/killall", "-9", "AudioComponentRegistrar" }))
            k.waitForProcessToFinish(5000);
    }
   #else
    juce::ignoreUnused(bundle, f);
   #endif
}

juce::String Installer::install(const PluginInfo& info, const std::vector<Format>& formats, const Progress& progress) {
    if (formats.empty()) return "Choose at least one format.";
    const auto* dl = info.download();
    if (dl == nullptr) return info.name + " isn't available for this computer yet.";
    for (auto f : formats)
        if (paths.pluginDirs.count(f) == 0 || info.bundleName(f).isEmpty())
            return info.name + " has no " + formatLabel(f) + " version for this computer.";

    const auto downloads = paths.dataDir.getChildFile("Downloads");
    if (! downloads.createDirectory()) return "Couldn't create " + downloads.getFullPathName();
    const auto zipFile = downloads.getChildFile(info.id + "-" + info.version + ".zip");
    if (auto err = download(*dl, zipFile, progress); err.isNotEmpty()) return err;

    if (progress) progress(0.86, "Unpacking");
    const auto staging = paths.dataDir.getChildFile("Staging").getChildFile(info.id + "-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()));
    const auto cleanup = [&] { staging.deleteRecursively(); zipFile.deleteFile(); };
    staging.createDirectory();
    {
        juce::ZipFile zip(zipFile);
        if (zip.getNumEntries() == 0) { cleanup(); return "The download isn't a valid zip file."; }
        for (int i = 0; i < zip.getNumEntries(); ++i) {   // refuse entries that would land outside staging
            const auto name = zip.getEntry(i)->filename;
            if (name.startsWith("/") || name.contains("..") || name.contains(":")) { cleanup(); return "The download contains unsafe paths."; }
        }
        if (auto r = zip.uncompressTo(staging, true); r.failed()) { cleanup(); return "Couldn't unpack: " + r.getErrorMessage(); }
    }

    // Check every requested bundle is in the zip before touching anything installed.
    std::vector<std::pair<juce::File, juce::File>> moves;   // staged -> destination
    for (auto f : formats) {
        const auto src = staging.getChildFile(zipFolder(f)).getChildFile(info.bundleName(f));
        if (! src.exists()) { cleanup(); return "The download has no " + formatLabel(f) + " version."; }
        moves.emplace_back(src, paths.pluginDirs[f].getChildFile(info.bundleName(f)));
    }

    if (progress) progress(0.92, "Installing");
    juce::StringArray installedFiles;
    for (size_t i = 0; i < moves.size(); ++i) {
        const auto& [src, dest] = moves[i];
        if (! dest.getParentDirectory().createDirectory()) { cleanup(); return "Couldn't create " + dest.getParentDirectory().getFullPathName(); }
        const auto backup = dest.getSiblingFile(dest.getFileName() + ".hub-old");
        removeItem(backup);
        if (dest.exists() && ! dest.moveFileTo(backup)) {
            cleanup();
            return "Couldn't replace " + dest.getFullPathName() + ". Close any app using it (your DAW) and try again.";
        }
        if (! moveItem(src, dest)) {
            backup.moveFileTo(dest);   // put the old version back
            cleanup();
            return "Couldn't write " + dest.getFullPathName();
        }
        removeItem(backup);
        afterInstall(dest, formats[i]);
        installedFiles.add(dest.getFullPathName());
    }

    // formats that were installed before but weren't chosen this time
    for (auto f : info.availableFormats())
        if (std::find(formats.begin(), formats.end(), f) == formats.end())
            if (auto it = paths.pluginDirs.find(f); it != paths.pluginDirs.end())
                removeItem(it->second.getChildFile(info.bundleName(f)));

    auto state = loadState();
    auto* entry = new juce::DynamicObject();
    entry->setProperty("version", info.version);
    juce::Array<juce::var> fmts;
    for (auto f : formats) fmts.add(formatKey(f));
    entry->setProperty("formats", fmts);
    entry->setProperty("files", installedFiles);
    entry->setProperty("installedAt", juce::Time::getCurrentTime().toISO8601(true));
    state["plugins"].getDynamicObject()->setProperty(info.id, juce::var(entry));
    saveState(state);

    cleanup();
    if (progress) progress(1.0, "Installed");
    return {};
}

juce::String Installer::uninstall(const PluginInfo& info) {
    juce::StringArray failed;
    for (auto f : info.availableFormats())
        if (auto it = paths.pluginDirs.find(f); it != paths.pluginDirs.end()) {
            const auto bundle = it->second.getChildFile(info.bundleName(f));
            if (! removeItem(bundle)) failed.add(bundle.getFullPathName());
        }
    auto state = loadState();
    state["plugins"].getDynamicObject()->removeProperty(info.id);
    saveState(state);
    if (failed.isEmpty()) return {};
    return "Couldn't remove " + failed.joinIntoString(", ") + ". Close your DAW and try again.";
}

} // namespace hub
