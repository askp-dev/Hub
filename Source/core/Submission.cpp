#include "Submission.h"
#include <juce_cryptography/juce_cryptography.h>

namespace hub {

namespace {
const juce::String& bundleExt(Format f) {
    static const juce::String vst3 = ".vst3", au = ".component", clap = ".clap";
    return f == Format::vst3 ? vst3 : f == Format::au ? au : clap;
}
std::optional<Format> formatForExtension(const juce::String& ext) {
    for (auto f : allFormats()) if (ext.equalsIgnoreCase(bundleExt(f))) return f;
    return std::nullopt;
}
juce::String platformName(const juce::String& p) { return p == "mac" ? "Mac" : "Windows"; }

bool isPublicHttps(const juce::String& s) {
    const juce::URL u(s);
    if (! s.startsWithIgnoreCase("https://") || s.containsAnyOf(" \t\r\n")) return false;
    const auto rest = s.fromFirstOccurrenceOf("://", false, false);
    const auto hostPort = rest.upToFirstOccurrenceOf("/", false, false).upToFirstOccurrenceOf("?", false, false);
    if (hostPort.containsChar('@') || hostPort.containsChar(':')) return false;   // no user:pass@, no port
    const auto host = hostPort.toLowerCase();
    if (! host.containsChar('.') || host == "localhost" || host.endsWith(".local") || host.endsWith(".internal")) return false;
    return ! host.containsOnly("0123456789.");
}

const juce::String sha256Of(const juce::File& f) { return juce::SHA256(f).toHexString().toLowerCase(); }

// Zip entries a picked zip offers: VST3/X.vst3/..., AU/X.component/..., CLAP/X.clap[/...]
juce::String readZipLayout(const juce::File& zipFile, const juce::String& platform, std::map<Format, juce::String>& names) {
    juce::ZipFile zip(zipFile);
    if (zip.getNumEntries() == 0) return zipFile.getFileName() + " isn't a zip file we can read.";
    bool macBinary = false, winBinary = false;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto path = zip.getEntry(i)->filename.replaceCharacter('\\', '/');
        if (path.startsWith("__MACOSX/")) continue;
        if (path.startsWith("/") || path.contains("..")) return zipFile.getFileName() + " contains unsafe paths.";
        const auto parts = juce::StringArray::fromTokens(path, "/", "");
        if (parts.size() < 2) continue;
        for (auto f : allFormats())
            if (parts[0] == zipFolder(f) && parts[1].endsWithIgnoreCase(bundleExt(f))) {
                if (auto it = names.find(f); it != names.end() && it->second != parts[1])
                    return zipFile.getFileName() + " has two " + formatLabel(f) + " bundles.";
                names[f] = parts[1];
            }
        if (path.contains("/Contents/MacOS/")) macBinary = true;
        if (path.contains("-win/") || (parts.size() == 2 && (parts[1].endsWithIgnoreCase(".clap") || parts[1].endsWithIgnoreCase(".vst3")))) winBinary = true;
    }
    if (names.empty())
        return zipFile.getFileName() + " has no VST3/, AU/ or CLAP/ folders inside. Pick the plugin bundles instead and the Hub zips them for you.";
    if (platform == "win" && names.count(Format::au)) return "AU is Mac only. That zip looks like a Mac build.";
    if (platform == "mac" && ! macBinary && winBinary) return zipFile.getFileName() + " looks like a Windows build.";
    if (platform == "win" && macBinary && ! winBinary) return zipFile.getFileName() + " looks like a Mac build.";
    return {};
}
} // namespace

juce::String Build::describe() const {
    if (names.empty()) return {};
    juce::StringArray fmts, stems;
    for (auto f : allFormats())
        if (auto it = names.find(f); it != names.end()) {
            fmts.add(formatLabel(f));
            stems.addIfNotAlreadyThere(juce::File::createLegalFileName(it->second).upToLastOccurrenceOf(".", false, false));
        }
    return fmts.joinIntoString(", ") + juce::String::fromUTF8("  \xc2\xb7  ") + stems.joinIntoString(", ")
         + (zip.existsAsFile() ? " (" + zip.getFileName() + ")" : juce::String());
}

bool bundleLooksLike(const juce::File& bundle, const juce::String& platform) {
    if (platform == "mac")
        return bundle.isDirectory() && bundle.getChildFile("Contents").getChildFile("MacOS").isDirectory();
    if (bundle.existsAsFile()) {   // a .clap, or an old single-file .vst3: a Windows DLL starts with "MZ"
        juce::FileInputStream in(bundle);
        char magic[2] = {};
        return in.openedOk() && in.read(magic, 2) == 2 && magic[0] == 'M' && magic[1] == 'Z';
    }
    for (const auto& d : bundle.getChildFile("Contents").findChildFiles(juce::File::findDirectories, false))
        if (d.getFileName().endsWithIgnoreCase("-win")) return true;   // x86_64-win, arm64-win, arm64ec-win
    return false;
}

juce::String addToBuild(Build& build, const juce::Array<juce::File>& picked) {
    std::map<Format, juce::File> found;
    for (const auto& f : picked) {
        const auto ext = f.getFileExtension();
        if (ext.equalsIgnoreCase(".zip") && f.existsAsFile()) {
            std::map<Format, juce::String> names;
            if (auto err = readZipLayout(f, build.platform, names); err.isNotEmpty()) return err;
            build.bundles.clear();
            build.zip = f;
            build.names = names;
            return {};
        }
        if (auto fmt = formatForExtension(ext)) {
            if (*fmt == Format::au && build.platform != "mac") return "AU is Mac only.";
            if (! bundleLooksLike(f, build.platform))
                return f.getFileName() + " doesn't look like a " + platformName(build.platform) + " build.";
            if (auto it = found.find(*fmt); it != found.end() && it->second != f)
                return "You picked two " + formatLabel(*fmt) + " bundles. Pick one of each format.";
            found[*fmt] = f;
            continue;
        }
        if (f.isDirectory()) {   // a build folder, e.g. MyPlugin_artefacts/Release
            juce::Array<juce::File> inside;
            for (auto fmt : allFormats()) {
                for (const auto& dir : { f.getChildFile(zipFolder(fmt)), f })
                    for (const auto& c : dir.findChildFiles(juce::File::findFilesAndDirectories, false, "*" + bundleExt(fmt)))
                        inside.addIfNotAlreadyThere(c);
            }
            if (build.platform != "mac")   // AU is Mac only
                inside.removeIf([](const juce::File& c) { return c.getFileExtension().equalsIgnoreCase(".component"); });
            if (inside.isEmpty())
                return "No " + juce::String(build.platform == "mac" ? "VST3, AU or CLAP" : "VST3 or CLAP") + " bundles in " + f.getFileName() + ".";
            for (const auto& c : inside) {
                const auto fmt = *formatForExtension(c.getFileExtension());
                if (! bundleLooksLike(c, build.platform))
                    return c.getFileName() + " doesn't look like a " + platformName(build.platform) + " build.";
                if (auto it = found.find(fmt); it != found.end() && it->second != c)
                    return f.getFileName() + " has two " + formatLabel(fmt) + " bundles. Pick the ones you want instead.";
                found[fmt] = c;
            }
            continue;
        }
        return f.getFileName() + " isn't a plugin bundle (.vst3, .component, .clap), a build folder or a zip.";
    }
    if (found.empty()) return "Nothing to add.";
    if (build.zip != juce::File()) { build.zip = juce::File(); build.bundles.clear(); build.names.clear(); }
    for (auto& [fmt, file] : found) { build.bundles[fmt] = file; build.names[fmt] = file.getFileName(); }
    return {};
}

const std::vector<std::pair<juce::String, juce::String>>& offerChoices() {
    static const std::vector<std::pair<juce::String, juce::String>> c {
        { "free", "Free" },
        { "store", "Sell it in the fueledby Store" },
        { "subscription", "Include it in the fueledby subscription" },
        { "undecided", "Not sure yet" },
    };
    return c;
}

juce::String statusLabel(const juce::String& s) {
    if (s == "uploading") return "Not sent (upload unfinished)";
    if (s == "pending") return "Waiting for review";
    if (s == "in_review") return "In review";
    if (s == "changes_requested") return "Changes requested";
    if (s == "approved") return "Approved";
    if (s == "rejected") return "Not accepted";
    if (s == "withdrawn") return "Withdrawn";
    return s;
}

juce::String validateForm(const SubmissionForm& f, const SubmitInfo& info, juce::String& field) {
    auto need = [&](const juce::String& v, const char* key, const juce::String& label, int minLen, int maxLen) -> juce::String {
        const auto t = v.trim();
        field = key;
        if (t.length() < minLen) return minLen <= 1 ? label + " is required." : label + " needs at least " + juce::String(minLen) + " characters.";
        if (t.length() > maxLen) return label + " can be at most " + juce::String(maxLen) + " characters.";
        field.clear();
        return {};
    };
    juce::String e;
    if ((e = need(f.name, "name", "Plugin name", 1, 60)).isNotEmpty()) return e;
    if ((e = need(f.version, "version", "Version", 1, 40)).isNotEmpty()) return e;
    {
        const auto v = f.version.trim().upToFirstOccurrenceOf("-", false, false).upToFirstOccurrenceOf("+", false, false);
        const auto parts = juce::StringArray::fromTokens(v, ".", "");
        bool ok = (parts.size() == 2 || parts.size() == 3);
        for (auto& p : parts) ok = ok && p.isNotEmpty() && p.length() <= 4 && p.containsOnly("0123456789");
        if (! ok) { field = "version"; return "Use a version like 1.0.0."; }
    }
    if ((e = need(f.tagline, "tagline", "Short description", 1, 140)).isNotEmpty()) return e;
    if ((e = need(f.description, "description", "Description", 20, 4000)).isNotEmpty()) return e;
    if (f.category.isEmpty()) { field = "category"; return "Pick a category."; }
    if ((e = need(f.creator, "creator", "Creator name", 1, 80)).isNotEmpty()) return e;
    if ((e = need(f.email, "email", "Email", 3, 254)).isNotEmpty()) return e;
    {
        const auto em = f.email.trim();
        const auto domain = em.fromLastOccurrenceOf("@", false, false);
        if (! em.containsChar('@') || em.indexOfChar('@') == 0 || ! domain.containsChar('.') || domain.endsWithChar('.')
            || domain.startsWithChar('.') || em.containsAnyOf(" \t<>(),;:\"[]\\")) { field = "email"; return "That email address doesn't look right."; }
    }
    if ((e = need(f.sourceUrl, "sourceUrl", "Source code link", 1, 500)).isNotEmpty()) return e;
    if (! isPublicHttps(f.sourceUrl.trim())) { field = "sourceUrl"; return "The source code link must be a public https:// address."; }
    if (f.homepage.trim().isNotEmpty() && ! isPublicHttps(f.homepage.trim())) { field = "homepage"; return "The website must be a public https:// address."; }
    if (f.license.isEmpty()) { field = "license"; return "Pick a license."; }
    if (f.license == "Other" && (e = need(f.licenseOther, "licenseOther", "License name", 2, 80)).isNotEmpty()) return e;
    if (f.offer.isEmpty()) { field = "offer"; return "Pick how you want to offer it."; }
    if (f.priceUsd.trim().isNotEmpty()) {
        const auto p = f.priceUsd.trim().trimCharactersAtStart("$");
        if (! p.containsOnly("0123456789.") || p.getDoubleValue() < 0.0 || p.getDoubleValue() > 999.0) {
            field = "priceUsd"; return "Price must be a number between 0 and 999 (USD).";
        }
    }
    if (f.mac.empty() && f.win.empty()) { field = "builds"; return "Add a Mac or Windows build."; }
    if (f.screenshots.size() > 4) { field = "screenshots"; return "Up to 4 screenshots."; }
    for (auto& s : f.screenshots) {
        const auto ext = s.getFileExtension().toLowerCase();
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") { field = "screenshots"; return "Screenshots must be PNG or JPEG."; }
        if (! s.existsAsFile()) { field = "screenshots"; return s.getFileName() + " is missing."; }
        if (s.getSize() > info.maxScreenshot) { field = "screenshots"; return s.getFileName() + " is over " + juce::File::descriptionOfSizeInBytes(info.maxScreenshot) + "."; }
    }
    if (f.testedIn.length() > 200) { field = "testedIn"; return "Tested in can be at most 200 characters."; }
    if (f.notes.length() > 2000) { field = "notes"; return "Notes can be at most 2000 characters."; }
    field = "agree";
    if (! f.agreeRights) return "Confirm you made this plugin or may distribute it.";
    if (! f.agreeSource) return "Confirm the source link is public and matches these builds.";
    if (! f.agreeTerms) return "Agree to the community creator terms to submit.";
    field.clear();
    return {};
}

// ------------------------------------------------------------------ client

SubmitClient::SubmitClient(juce::String baseUrl) : base(baseUrl.trimCharactersAtEnd("/")) {}

juce::String SubmitClient::fileSlug(const juce::String& name) {
    juce::String s;
    for (auto c : name.toLowerCase())
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s << juce::String::charToString(c);
    return s.isEmpty() ? juce::String("plugin") : s.substring(0, 24);
}

SubmitClient::Reply SubmitClient::request(const juce::String& method, const juce::String& path, const juce::MemoryBlock* body,
                                          const juce::String& contentType, const juce::String& token, int timeoutMs) const {
    Reply r;
    juce::URL url(base + path);
    if (body != nullptr) url = url.withPOSTData(*body);
    juce::String headers = "User-Agent: fueledby-hub/" + juce::String(HUB_VERSION) + "\r\nAccept: application/json\r\n";
    if (contentType.isNotEmpty()) headers << "Content-Type: " << contentType << "\r\n";
    if (token.isNotEmpty()) headers << "Authorization: Bearer " << token << "\r\n";
    auto in = url.createInputStream(juce::URL::InputStreamOptions(body != nullptr ? juce::URL::ParameterHandling::inPostData
                                                                                  : juce::URL::ParameterHandling::inAddress)
                                        .withConnectionTimeoutMs(timeoutMs)
                                        .withNumRedirectsToFollow(0)
                                        .withStatusCode(&r.status)
                                        .withHttpRequestCmd(method)
                                        .withExtraHeaders(headers));
    if (in == nullptr || r.status == 0) {
        r.error = "Couldn't reach the fueledby submissions service. Check your internet connection and try again.";
        return r;
    }
    const auto text = in->readEntireStreamAsString();
    r.json = juce::JSON::parse(text);
    if (r.status >= 400) {
        r.error = r.json.getProperty("error", {}).toString();
        if (r.error.isEmpty()) r.error = "The submissions service answered with HTTP " + juce::String(r.status) + ".";
    }
    return r;
}

SubmitInfo SubmitClient::fetchInfo() const {
    SubmitInfo info;
    const auto r = request("GET", "/v1/info", nullptr, {}, {}, 10000);
    if (r.error.isNotEmpty() || ! r.json.isObject()) {
        info.error = r.error.isNotEmpty() ? r.error : "The submissions service sent something unexpected.";
        return info;
    }
    info.reachable = true;
    info.open = (bool) r.json.getProperty("open", false);
    info.termsUrl = r.json.getProperty("termsUrl", info.termsUrl).toString();
    info.termsVersion = r.json.getProperty("termsVersion", {}).toString();
    info.partSize = (juce::int64) r.json.getProperty("partSize", (juce::int64) info.partSize);
    info.maxBuild = (juce::int64) r.json.getProperty("maxBuildBytes", info.maxBuild);
    info.maxScreenshot = (juce::int64) r.json.getProperty("maxScreenshotBytes", info.maxScreenshot);
    info.maxTotal = (juce::int64) r.json.getProperty("maxTotalBytes", info.maxTotal);
    if (auto* a = r.json.getProperty("categories", {}).getArray()) for (auto& v : *a) info.categories.add(v.toString());
    if (auto* a = r.json.getProperty("licenses", {}).getArray()) for (auto& v : *a) info.licenses.add(v.toString());
    return info;
}

juce::String SubmitClient::packageBuild(const Build& build, const juce::File& destZip) {
    destZip.deleteFile();
    if (build.zip.existsAsFile())
        return build.zip.copyFileTo(destZip) ? juce::String() : "Couldn't copy " + build.zip.getFileName();
    juce::ZipFile::Builder zip;
    for (const auto& [fmt, bundle] : build.bundles) {
        const auto root = zipFolder(fmt) + "/" + bundle.getFileName();
        if (bundle.isDirectory()) {
            for (const auto& f : bundle.findChildFiles(juce::File::findFiles, true, "*", juce::File::FollowSymlinks::no)) {
                if (f.getFileName() == ".DS_Store") continue;
                zip.addFile(f, 6, root + "/" + f.getRelativePathFrom(bundle).replaceCharacter('\\', '/'));
            }
        } else if (bundle.existsAsFile()) {
            zip.addFile(bundle, 6, root);
        } else {
            return bundle.getFullPathName() + " is missing.";
        }
    }
    destZip.getParentDirectory().createDirectory();
    juce::FileOutputStream os(destZip);
    if (! os.openedOk()) return "Couldn't write " + destZip.getFullPathName();
    if (! zip.writeToStream(os, nullptr)) return "Couldn't zip the build.";
    os.flush();
    return os.getStatus().failed() ? "The disk is full or not writable." : juce::String();
}

juce::String SubmitClient::submit(const SubmissionForm& form, const SubmitInfo& info, const juce::File& workDir,
                                  SubmissionRecord& out, const Progress& progress) const {
    juce::String field;
    if (auto err = validateForm(form, info, field); err.isNotEmpty()) return err;
    if (! workDir.createDirectory()) return "Couldn't create " + workDir.getFullPathName();
    struct Item { juce::File file; juce::String name, kind, sha; juce::int64 size = 0; const Build* build = nullptr; bool temp = false; };
    std::vector<Item> items;
    const struct Cleanup { std::vector<Item>& items; ~Cleanup() { for (auto& i : items) if (i.temp) i.file.deleteFile(); } } cleanup { items };
    auto tell = [&](double f, const juce::String& t) { return ! progress || progress(f, t); };

    const auto stem = fileSlug(form.name) + "-" + form.version.trim().replaceCharacters("+", "-");
    for (const Build* b : { &form.mac, &form.win }) {
        if (b->empty()) continue;
        if (! tell(-1.0, "Zipping the " + platformName(b->platform) + " build")) return "Cancelled";
        Item it;
        it.name = stem + "-" + b->platform + ".zip";
        it.kind = "build-" + b->platform;
        it.build = b;
        if (b->zip.existsAsFile()) it.file = b->zip;
        else {
            it.file = workDir.getChildFile(it.name);
            it.temp = true;
            if (auto err = packageBuild(*b, it.file); err.isNotEmpty()) return err;
        }
        it.size = it.file.getSize();
        if (it.size > info.maxBuild)
            return "The " + platformName(b->platform) + " build is " + juce::File::descriptionOfSizeInBytes(it.size)
                 + ", over the " + juce::File::descriptionOfSizeInBytes(info.maxBuild) + " limit.";
        items.push_back(it);
    }
    for (int i = 0; i < form.screenshots.size(); ++i) {
        const auto& s = form.screenshots.getReference(i);
        Item it;
        it.file = s;
        it.kind = "screenshot";
        it.name = "screenshot-" + juce::String(i + 1) + s.getFileExtension().toLowerCase();
        it.size = s.getSize();
        items.push_back(it);
    }
    juce::int64 total = 0;
    for (auto& it : items) {
        if (! tell(-1.0, "Checking " + it.name)) return "Cancelled";
        it.sha = sha256Of(it.file);
        total += it.size;
    }
    if (total > info.maxTotal)
        return "All files together are " + juce::File::descriptionOfSizeInBytes(total) + ", over the "
             + juce::File::descriptionOfSizeInBytes(info.maxTotal) + " limit.";

    // ---- create
    auto* plugin = new juce::DynamicObject();
    plugin->setProperty("name", form.name.trim());
    plugin->setProperty("creator", form.creator.trim());
    plugin->setProperty("email", form.email.trim());
    plugin->setProperty("version", form.version.trim());
    plugin->setProperty("tagline", form.tagline.trim());
    plugin->setProperty("description", form.description.trim());
    plugin->setProperty("category", form.category);
    plugin->setProperty("sourceUrl", form.sourceUrl.trim());
    if (form.homepage.trim().isNotEmpty()) plugin->setProperty("homepage", form.homepage.trim());
    plugin->setProperty("license", form.license);
    if (form.license == "Other") plugin->setProperty("licenseOther", form.licenseOther.trim());
    plugin->setProperty("offer", form.offer);
    if (form.priceUsd.trim().isNotEmpty()) plugin->setProperty("priceUsd", form.priceUsd.trim().trimCharactersAtStart("$").getDoubleValue());
    if (form.testedIn.trim().isNotEmpty()) plugin->setProperty("testedIn", form.testedIn.trim());
    if (form.notes.trim().isNotEmpty()) plugin->setProperty("notes", form.notes.trim());
    juce::Array<juce::var> files;
    for (auto& it : items) {
        auto* f = new juce::DynamicObject();
        f->setProperty("name", it.name);
        f->setProperty("kind", it.kind);
        f->setProperty("size", it.size);
        f->setProperty("sha256", it.sha);
        if (it.build != nullptr) {
            auto* b = new juce::DynamicObject();
            for (auto& [fmt, n] : it.build->names) b->setProperty(formatKey(fmt), n);
            f->setProperty("bundles", juce::var(b));
        }
        files.add(juce::var(f));
    }
    auto* agree = new juce::DynamicObject();
    agree->setProperty("rights", form.agreeRights);
    agree->setProperty("source", form.agreeSource);
    agree->setProperty("terms", form.agreeTerms);
    agree->setProperty("termsVersion", info.termsVersion);
    auto* root = new juce::DynamicObject();
    root->setProperty("plugin", juce::var(plugin));
    root->setProperty("files", files);
    root->setProperty("agree", juce::var(agree));
    root->setProperty("hubVersion", juce::String(HUB_VERSION));
    const auto json = juce::JSON::toString(juce::var(root), true);
    juce::MemoryBlock body(json.toRawUTF8(), json.getNumBytesAsUTF8());
    if (! tell(0.02, "Starting the submission")) return "Cancelled";
    const auto created = request("POST", "/v1/submissions", &body, "application/json", {});
    if (created.error.isNotEmpty()) return created.error;
    out = {};
    out.id = created.json.getProperty("id", {}).toString();
    out.ref = created.json.getProperty("ref", {}).toString();
    out.token = created.json.getProperty("token", {}).toString();
    out.name = form.name.trim();
    out.version = form.version.trim();
    out.status = "uploading";
    const juce::int64 partSize = juce::jmax((juce::int64) (5 << 20), (juce::int64) created.json.getProperty("partSize", (juce::int64) info.partSize));
    if (out.id.isEmpty() || out.token.isEmpty()) return "The submissions service sent something unexpected.";

    // ---- upload, part by part
    juce::int64 sent = 0;
    for (auto& it : items) {
        juce::FileInputStream in(it.file);
        if (! in.openedOk()) return "Couldn't read " + it.file.getFullPathName();
        const int parts = (int) juce::jmax((juce::int64) 1, (it.size + partSize - 1) / partSize);
        for (int n = 1; n <= parts; ++n) {
            const auto len = (size_t) juce::jmin(partSize, it.size - (juce::int64) (n - 1) * partSize);
            juce::MemoryBlock chunk(len);
            in.setPosition((juce::int64) (n - 1) * partSize);
            if ((size_t) in.read(chunk.getData(), (int) len) != len) return "Couldn't read " + it.file.getFullPathName();
            juce::String err;
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (! tell(0.05 + 0.9 * (double) sent / (double) juce::jmax((juce::int64) 1, total),
                           "Uploading " + it.name + juce::String::fromUTF8("  \xc2\xb7  ") + juce::File::descriptionOfSizeInBytes(sent)
                               + " of " + juce::File::descriptionOfSizeInBytes(total)))
                    return "Cancelled";
                const auto r = request("PUT", "/v1/submissions/" + out.id + "/files/" + juce::URL::addEscapeChars(it.name, false)
                                              + "/parts/" + juce::String(n), &chunk, "application/octet-stream", out.token, 120000);
                err = r.error;
                if (err.isEmpty() || (r.status >= 400 && r.status < 500)) break;   // retry only network and server errors
                juce::Thread::sleep(1000 * (attempt + 1));
            }
            if (err.isNotEmpty()) return err;
            sent += (juce::int64) len;
        }
    }

    // ---- send
    if (! tell(0.97, "Sending")) return "Cancelled";
    const auto fin = request("POST", "/v1/submissions/" + out.id + "/finalize", nullptr, {}, out.token, 60000);
    if (fin.error.isNotEmpty()) return fin.error;
    out.status = fin.json.getProperty("status", "pending").toString();
    out.submittedAt = juce::Time::currentTimeMillis();
    tell(1.0, "Sent");
    return {};
}

juce::String SubmitClient::refresh(SubmissionRecord& rec) const {
    const auto r = request("GET", "/v1/submissions/" + rec.id, nullptr, {}, rec.token, 10000);
    if (r.error.isNotEmpty()) return r.error;
    rec.status = r.json.getProperty("status", rec.status).toString();
    const auto note = r.json.getProperty("reviewNote", {});
    rec.reviewNote = note.isVoid() || note.isUndefined() ? juce::String() : note.toString();
    return {};
}

juce::String SubmitClient::withdraw(SubmissionRecord& rec) const {
    const auto r = request("DELETE", "/v1/submissions/" + rec.id, nullptr, {}, rec.token, 10000);
    if (r.error.isNotEmpty()) return r.error;
    rec.status = r.json.getProperty("status", rec.status).toString();
    return {};
}

std::vector<SubmissionRecord> SubmitClient::loadRecords(const juce::File& file) {
    std::vector<SubmissionRecord> out;
    const auto parsed = juce::JSON::parse(file);   // keep it alive while iterating
    if (auto* a = parsed.getArray())
        for (auto& v : *a) {
            SubmissionRecord r;
            r.id = v["id"].toString(); r.ref = v["ref"].toString(); r.token = v["token"].toString();
            r.name = v["name"].toString(); r.version = v["version"].toString(); r.status = v["status"].toString();
            r.reviewNote = v["reviewNote"].toString(); r.submittedAt = (juce::int64) v["submittedAt"];
            if (r.id.isNotEmpty() && r.token.isNotEmpty()) out.push_back(r);
        }
    return out;
}

void SubmitClient::saveRecords(const juce::File& file, const std::vector<SubmissionRecord>& recs) {
    juce::Array<juce::var> a;
    for (auto& r : recs) {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", r.id); o->setProperty("ref", r.ref); o->setProperty("token", r.token);
        o->setProperty("name", r.name); o->setProperty("version", r.version); o->setProperty("status", r.status);
        o->setProperty("reviewNote", r.reviewNote); o->setProperty("submittedAt", r.submittedAt);
        a.add(juce::var(o));
    }
    file.getParentDirectory().createDirectory();
    const auto tmp = file.getSiblingFile(file.getFileName() + ".tmp");
    if (tmp.replaceWithText(juce::JSON::toString(a))) tmp.moveFileTo(file);
}

} // namespace hub
