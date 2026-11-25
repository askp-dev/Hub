// Installer tests: run everything against a sandbox folder with a fake plugin
// zip, so nothing touches the real plug-in folders.
#include "core/Installer.h"
#include "core/Submission.h"
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

    // Rebrand: data and plugins in the old askp folders move to the new ones, once.
    {
        const auto r2 = root.getChildFile("rebrand");
        Paths p = Paths::sandbox(r2);
        const auto write = [](const juce::File& f, const char* text) { f.getParentDirectory().createDirectory(); f.replaceWithText(text); };
        p.legacyDataDir = r2.getChildFile("askp Hub");
        write(p.legacyDataDir.getChildFile("installed.json"), R"({"plugins":{"demo":{"version":"1.0.0"}}})");
        p.legacyPluginDirs[Format::vst3] = r2.getChildFile("OldVST3");
        write(p.legacyPluginDirs[Format::vst3].getChildFile("Demo.vst3/Contents/x"), "old");
        write(p.legacyPluginDirs[Format::vst3].getChildFile("Other.vst3/Contents/x"), "old");
        write(p.pluginDirs[Format::vst3].getChildFile("Other.vst3/Contents/x"), "new");   // already moved
        Installer mig(p);
        mig.migrateLegacy();
        CHECK(p.dataDir.getChildFile("installed.json").existsAsFile() && ! p.legacyDataDir.exists());
        CHECK(p.pluginDirs[Format::vst3].getChildFile("Demo.vst3/Contents/x").loadFileAsString() == "old");
        CHECK(p.pluginDirs[Format::vst3].getChildFile("Other.vst3/Contents/x").loadFileAsString() == "new");
        CHECK(! p.legacyPluginDirs[Format::vst3].exists());
        CHECK(mig.status(v1).managed);
        mig.migrateLegacy();   // nothing left to do
        CHECK(p.dataDir.getChildFile("installed.json").existsAsFile());
    }

    // ---------------------------------------------------------------- plugin submissions
    {
        const auto sub = root.getChildFile("submit");
        auto write = [](const juce::File& f, const juce::String& text) { f.getParentDirectory().createDirectory(); f.replaceWithText(text); };
        // a Mac build folder like JUCE's artefacts: VST3/, AU/, CLAP/ bundles with Contents/MacOS
        const auto rel = sub.getChildFile("Moss_artefacts/Release");
        write(rel.getChildFile("VST3/Moss.vst3/Contents/MacOS/Moss"), "mac binary");
        write(rel.getChildFile("VST3/Moss.vst3/Contents/Info.plist"), "plist");
        write(rel.getChildFile("AU/Moss.component/Contents/MacOS/Moss"), "mac binary");
        write(rel.getChildFile("CLAP/Moss.clap/Contents/MacOS/Moss"), "mac binary");
        write(rel.getChildFile("Standalone/Moss.app/Contents/MacOS/Moss"), "app");   // ignored
        // a Windows build: VST3 folder bundle + single-file CLAP (a DLL)
        write(sub.getChildFile("win/Moss.vst3/Contents/x86_64-win/Moss.vst3"), "MZ dll");
        write(sub.getChildFile("win/Moss.clap"), "MZ dll");

        Build mac { "mac" };
        CHECK_OK(addToBuild(mac, { rel }));
        CHECK(mac.names.size() == 3 && mac.names[Format::au] == "Moss.component");
        CHECK(mac.describe().startsWith("VST3, AU, CLAP"));

        Build win { "win" };
        CHECK(addToBuild(win, { rel.getChildFile("VST3/Moss.vst3") }).contains("doesn't look like a Windows build"));
        CHECK(addToBuild(win, { rel.getChildFile("AU/Moss.component") }).contains("Mac only"));
        CHECK_OK(addToBuild(win, { sub.getChildFile("win/Moss.vst3") }));
        CHECK_OK(addToBuild(win, { sub.getChildFile("win/Moss.clap") }));   // added one at a time
        CHECK(win.names.size() == 2);
        Build macWrong { "mac" };
        CHECK(addToBuild(macWrong, { sub.getChildFile("win/Moss.clap") }).isNotEmpty());
        CHECK(addToBuild(macWrong, { sub.getChildFile("win") }).isNotEmpty());   // Windows bundles in a folder
        CHECK(addToBuild(macWrong, { rel.getChildFile("Standalone") }).contains("No VST3"));

        // zips in the Hub's install layout, and a picked zip
        const auto macZip = sub.getChildFile("out/moss-mac.zip");
        CHECK_OK(SubmitClient::packageBuild(mac, macZip));
        {
            juce::ZipFile z(macZip);
            CHECK(z.getEntry("VST3/Moss.vst3/Contents/MacOS/Moss") != nullptr);
            CHECK(z.getEntry("AU/Moss.component/Contents/MacOS/Moss") != nullptr);
            CHECK(z.getEntry("CLAP/Moss.clap/Contents/MacOS/Moss") != nullptr);
        }
        const auto winZip = sub.getChildFile("out/moss-win.zip");
        CHECK_OK(SubmitClient::packageBuild(win, winZip));
        {
            juce::ZipFile z(winZip);
            CHECK(z.getEntry("CLAP/Moss.clap") != nullptr);
            CHECK(z.getEntry("VST3/Moss.vst3/Contents/x86_64-win/Moss.vst3") != nullptr);
        }
        Build fromZip { "mac" };
        CHECK_OK(addToBuild(fromZip, { macZip }));
        CHECK(fromZip.zip == macZip && fromZip.names.size() == 3);
        Build zipWrongPlatform { "win" };
        CHECK(addToBuild(zipWrongPlatform, { macZip }).isNotEmpty());
        Build zipForWin { "win" };
        CHECK_OK(addToBuild(zipForWin, { winZip }));
        CHECK(zipForWin.names.size() == 2);
        const auto junk = sub.getChildFile("junk.zip");
        { juce::ZipFile::Builder zb; write(sub.getChildFile("readme.txt"), "hi"); zb.addFile(sub.getChildFile("readme.txt"), 0, "readme.txt");
          juce::FileOutputStream os(junk); zb.writeToStream(os, nullptr); }
        Build j { "mac" };
        CHECK(addToBuild(j, { junk }).contains("no VST3/"));

        // the form, checked like the service does
        SubmitInfo info;
        info.termsVersion = "2026-11-25";
        SubmissionForm f;
        f.name = "Moss Verb"; f.version = "1.0.0"; f.tagline = "A warm plate"; f.description = "A plate reverb with a freeze switch.";
        f.category = "Reverb & space"; f.creator = "Rae"; f.email = "rae@example.com"; f.sourceUrl = "https://github.com/rae/moss";
        f.license = "GPL-3.0"; f.offer = "free"; f.mac = mac;
        f.agreeRights = f.agreeSource = f.agreeTerms = true;
        juce::String field;
        CHECK_OK(validateForm(f, info, field));
        auto expectField = [&](SubmissionForm g, const char* key) { juce::String fl; validateForm(g, info, fl); CHECK(fl == key); };
        { auto g = f; g.email = "rae@"; expectField(g, "email"); }
        { auto g = f; g.email = ""; expectField(g, "email"); }
        { auto g = f; g.sourceUrl = ""; expectField(g, "sourceUrl"); }
        { auto g = f; g.sourceUrl = "http://github.com/rae/moss"; expectField(g, "sourceUrl"); }
        { auto g = f; g.sourceUrl = "https://localhost/x"; expectField(g, "sourceUrl"); }
        { auto g = f; g.sourceUrl = "https://me:pw@github.com/x"; expectField(g, "sourceUrl"); }
        { auto g = f; g.version = "v1"; expectField(g, "version"); }
        { auto g = f; g.description = "short"; expectField(g, "description"); }
        { auto g = f; g.mac = Build { "mac" }; expectField(g, "builds"); }
        { auto g = f; g.license = "Other"; expectField(g, "licenseOther"); }
        { auto g = f; g.priceUsd = "lots"; expectField(g, "priceUsd"); }
        { auto g = f; g.agreeTerms = false; expectField(g, "agree"); }
        { auto g = f; g.screenshots.add(sub.getChildFile("shot.gif")); expectField(g, "screenshots"); }
        CHECK(SubmitClient::fileSlug("Moss Verb!") == "mossverb");

        // the Hub's list of sent submissions survives a round trip
        const auto recs = sub.getChildFile("Data/submissions.json");
        SubmissionRecord r; r.id = "sub_0123456789abcdef"; r.ref = "FB-7K3Q2"; r.token = "t"; r.name = "Moss Verb"; r.version = "1.0.0";
        r.status = "pending"; r.submittedAt = 1764072000000;
        SubmitClient::saveRecords(recs, { r });
        const auto back = SubmitClient::loadRecords(recs);
        CHECK(back.size() == 1 && back[0].ref == "FB-7K3Q2" && back[0].submittedAt == 1764072000000 && back[0].token == "t");
        CHECK(statusLabel("changes_requested") == "Changes requested");

        // no service there: a clear message, not a hang
        const auto offline = SubmitClient("http://127.0.0.1:9").fetchInfo();
        CHECK(! offline.reachable && offline.error.isNotEmpty());
    }

    root.deleteRecursively();
    std::cout << (failures == 0 ? "hub_tests: all passed\n" : "hub_tests: FAILED\n");
    return failures == 0 ? 0 : 1;
}
