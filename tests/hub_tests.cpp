// Installer tests: run everything against a sandbox folder with a fake plugin
// zip, so nothing touches the real plug-in folders.
#include "core/Installer.h"
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>
#include <iostream>

using namespace hub;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)
#define CHECK_OK(err) do { const juce::String e_ = (err); if (e_.isNotEmpty()) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " << e_ << "\n"; } } while (0)

// A zip laid out like JUCE's artefacts folder: VST3/Demo.vst3, AU/Demo.component, CLAP/Demo.clap
static juce::File makeZip(const juce::File& dir, const juce::String& version, bool withClap = true) {
    const auto src = dir.getChildFile("src-" + version);
    src.deleteRecursively();
    const juce::String plist = "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>CFBundleShortVersionString</key><string>"
                               + version + "</string></dict></plist>";
    juce::ZipFile::Builder zip;
    auto add = [&](const juce::String& path, const juce::String& content) {
        const auto f = src.getChildFile(path);
        f.getParentDirectory().createDirectory();
        f.replaceWithText(content);
        zip.addFile(f, 9, path);
    };
    add("VST3/Demo.vst3/Contents/Info.plist", plist);
    add("VST3/Demo.vst3/Contents/MacOS/Demo", "binary " + version);
    add("AU/Demo.component/Contents/Info.plist", plist);
    if (withClap) add("CLAP/Demo.clap/Contents/Info.plist", plist);
    const auto out = dir.getChildFile("demo-" + version + ".zip");
    out.deleteFile();
    juce::FileOutputStream os(out);
    zip.writeToStream(os, nullptr);
    os.flush();
    return out;
}

static juce::String manifestFor(const juce::File& zip, const juce::String& version, const juce::String& sha) {
    return R"({ "schema": 1, "hub": { "version": "0.2.0", "download": { "mac": "https://example.com/Hub.dmg" } },
      "plugins": [ { "id": "demo", "name": "Demo", "badge": "DM", "tagline": "Test", "version": ")" + version + R"(",
        "bundles": { "vst3": "Demo.vst3", "au": "Demo.component", "clap": "Demo.clap" },
        "downloads": { ")" + platformKey() + R"(": { "url": ")" + juce::URL(zip).toString(false) + R"(",
                       "sha256": ")" + sha + R"(", "size": )" + juce::String(zip.getSize()) + R"( } } },
        { "id": "bad", "name": "Bad", "version": "1.0", "bundles": { "vst3": "../escape.vst3" } },
        { "id": "noversion", "name": "No version", "bundles": { "vst3": "X.vst3" } } ] })";
}

static PluginInfo load(const juce::String& json) {
    juce::String err;
    auto m = Manifest::parse(json, err);
    CHECK(m.has_value());
    CHECK(m && m->plugins.size() == 1);   // the two broken entries are skipped
    return m ? m->plugins[0] : PluginInfo {};
}

static bool has(const InstallStatus& s, Format f) { return std::find(s.formats.begin(), s.formats.end(), f) != s.formats.end(); }

int main() {
    juce::ScopedJuceInitialiser_GUI init;

    // versions
    CHECK(compareVersions("0.10.0", "0.9.2") > 0);
    CHECK(compareVersions("1.0", "1.0.0") == 0);
    CHECK(compareVersions("1.2.3-beta", "1.2.3") == 0);
    CHECK(compareVersions("0.1.0", "0.1.1") < 0);

    // manifest errors
    juce::String err;
    CHECK(! Manifest::parse("not json", err));
    CHECK(! Manifest::parse(R"({"schema": 99, "plugins": []})", err) && err.contains("newer Hub"));
    CHECK(! Manifest::parse(R"({"plugins": []})", err));

    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hub-tests-" + juce::String(juce::Time::currentTimeMillis()));
    root.createDirectory();
    Installer inst(Paths::sandbox(root));
   #if JUCE_MAC
    const std::vector<Format> all { Format::vst3, Format::au, Format::clap };
   #else
    const std::vector<Format> all { Format::vst3, Format::clap };   // no AU outside macOS
   #endif

    const auto zip1 = makeZip(root, "1.0.0");
    const auto sha1 = juce::SHA256(zip1).toHexString();
    auto v1 = load(manifestFor(zip1, "1.0.0", sha1));
    CHECK(v1.availableFormats() == all);
    {   // a zip that says it only holds VST3 only offers VST3
        auto narrow = load(manifestFor(zip1, "1.0.0", sha1).replace("\"size\":", "\"formats\": [\"vst3\"], \"size\":"));
        CHECK(narrow.availableFormats() == std::vector<Format> { Format::vst3 });
    }
    CHECK(! inst.status(v1).installed);

    // fresh install, with progress reaching 1
    double last = 0;
    CHECK_OK(inst.install(v1, all, [&](double f, const juce::String&) { if (f >= 0) last = f; return true; }));
    CHECK(last == 1.0);
    auto s = inst.status(v1);
    CHECK(s.installed && s.managed && s.version == "1.0.0" && s.formats == all);
    CHECK(inst.getPaths().pluginDirs.at(Format::vst3).getChildFile("Demo.vst3/Contents/MacOS/Demo").loadFileAsString() == "binary 1.0.0");
    CHECK(! root.getChildFile("Data/Staging").getNumberOfChildFiles(juce::File::findFilesAndDirectories));   // cleaned up
    CHECK(! root.getChildFile("Data/Downloads").getNumberOfChildFiles(juce::File::findFiles));

    // a bad checksum installs nothing and keeps 1.0.0
    const auto zip2 = makeZip(root, "2.0.0");
    auto v2bad = load(manifestFor(zip2, "2.0.0", juce::String::repeatedString("0", 64)));
    CHECK(inst.install(v2bad, all).contains("checksum"));
    CHECK(inst.status(v1).version == "1.0.0");

    // cancelling during the download keeps 1.0.0
    auto v2 = load(manifestFor(zip2, "2.0.0", juce::SHA256(zip2).toHexString()));
    CHECK(inst.install(v2, all, [](double, const juce::String&) { return false; }) == "Cancelled");
    CHECK(inst.status(v1).version == "1.0.0");

    // update
    CHECK(compareVersions(inst.status(v2).version, v2.version) < 0);
    CHECK_OK(inst.install(v2, all));
    CHECK(inst.status(v2).version == "2.0.0");
    CHECK(inst.getPaths().pluginDirs.at(Format::vst3).getChildFile("Demo.vst3/Contents/MacOS/Demo").loadFileAsString() == "binary 2.0.0");
    CHECK(! inst.getPaths().pluginDirs.at(Format::vst3).getChildFile("Demo.vst3.hub-old").exists());

    // narrowing formats removes the one left out
    CHECK_OK(inst.install(v2, { Format::vst3 }));
    s = inst.status(v2);
    CHECK(has(s, Format::vst3) && ! has(s, Format::clap));

    // a zip missing a format fails before touching anything
    const auto zip3 = makeZip(root, "3.0.0", false);
    auto v3 = load(manifestFor(zip3, "3.0.0", juce::SHA256(zip3).toHexString()));
    CHECK(inst.install(v3, { Format::vst3, Format::clap }).contains("no CLAP"));
    CHECK(inst.status(v3).version == "2.0.0");

    // a copy found outside the Hub (no state entry): version comes from Info.plist,
    // and an old system-wide copy is reported
    CHECK_OK(inst.uninstall(v2));
    CHECK(! inst.status(v2).installed);
    const auto manual = inst.getPaths().pluginDirs.at(Format::vst3).getChildFile("Demo.vst3/Contents/Info.plist");
    manual.getParentDirectory().createDirectory();
    manual.replaceWithText("<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>CFBundleShortVersionString</key><string>0.9.0</string></dict></plist>");
    inst.getPaths().systemDirs.at(Format::vst3).getChildFile("Demo.vst3").createDirectory();
    s = inst.status(v2);
    CHECK(s.installed && ! s.managed && s.version == "0.9.0" && s.elsewhere.size() == 1);
    CHECK_OK(inst.uninstall(v2));
    CHECK(! inst.status(v2).installed);

    // optional: a real HTTP download (HUB_TEST_HTTP_DIR is served at HUB_TEST_HTTP_BASE)
    if (const auto dir = juce::SystemStats::getEnvironmentVariable("HUB_TEST_HTTP_DIR", {}); dir.isNotEmpty()) {
        zip2.copyFileTo(juce::File(dir).getChildFile("demo-2.0.0.zip"));
        auto web = v2;
        web.downloads[platformKey()].url = juce::SystemStats::getEnvironmentVariable("HUB_TEST_HTTP_BASE", {}) + "/demo-2.0.0.zip";
        CHECK_OK(inst.install(web, all));
        CHECK(inst.status(web).version == "2.0.0");
        web.downloads[platformKey()].url += ".missing";
        CHECK(inst.install(web, all).contains("HTTP 404"));
        CHECK_OK(inst.uninstall(web));
        std::cout << "http download checked\n";
    }

    // missing download
    auto none = v1; none.downloads.clear();
    CHECK(inst.install(none, all).contains("isn't available"));

    root.deleteRecursively();
    std::cout << (failures == 0 ? "hub_tests: all passed\n" : "hub_tests: FAILED\n");
    return failures == 0 ? 0 : 1;
}
