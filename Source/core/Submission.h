#pragma once
// "Submit a plugin": community creators send a plugin to the fueledby team from the Hub.
// No UI in here, so the tests and the headless --submit-json path drive the same code.
//
// The service (submissions/ in this repo, a Cloudflare Worker) keeps files in R2 and details in D1,
// and puts every submission in a review queue. Nothing is published automatically.
#include "Manifest.h"
#include <functional>
#include <map>

namespace hub {

// A build for one platform: bundles the creator picked (the Hub zips them in the same VST3/ AU/ CLAP/
// layout the Hub installs from), or a zip that already has that layout.
struct Build {
    juce::String platform;                   // "mac" or "win"
    std::map<Format, juce::File> bundles;    // when picked as bundles or a build folder
    juce::File zip;                          // when picked as a ready zip
    std::map<Format, juce::String> names;    // bundle file name per format (both cases)
    bool empty() const { return names.empty(); }
    juce::String describe() const;           // "VST3, AU, CLAP · MossVerb"
};

// Reads what was picked or dropped for one platform: .vst3 / .component / .clap bundles, a JUCE
// "<Target>_artefacts/Release" folder (VST3/ AU/ CLAP/ inside), or a .zip laid out like that.
// Adds to `build` (so bundles can be picked one at a time). Returns an error, empty on success.
juce::String addToBuild(Build& build, const juce::Array<juce::File>& picked);

// True when a bundle looks built for that platform (Contents/MacOS on Mac, a Windows DLL on Windows).
bool bundleLooksLike(const juce::File& bundle, const juce::String& platform);

struct SubmissionForm {
    juce::String name, creator, email, version, tagline, description, category, sourceUrl, homepage,
                 license, licenseOther, offer, priceUsd, testedIn, notes;
    bool agreeRights = false, agreeSource = false, agreeTerms = false;
    Build mac { "mac" }, win { "win" };
    juce::Array<juce::File> screenshots;
};

// What the service says about itself (GET /v1/info).
struct SubmitInfo {
    bool reachable = false, open = false;
    juce::String termsUrl { "https://fueledby.cc/terms" }, termsVersion;
    juce::int64 partSize = 8 << 20, maxBuild = 300ll << 20, maxScreenshot = 8 << 20, maxTotal = 700ll << 20;
    juce::StringArray categories, licenses;
    juce::String error;   // why it isn't reachable
};

// One submission this computer sent, kept in the Hub's data folder so its status can be checked.
struct SubmissionRecord {
    juce::String id, ref, token, name, version, status, reviewNote;
    juce::int64 submittedAt = 0;
};

// Offer choices: key sent to the service, label shown in the Hub.
const std::vector<std::pair<juce::String, juce::String>>& offerChoices();
juce::String statusLabel(const juce::String& status);   // "pending" -> "Waiting for review"

// Same rules as the service (submissions/src/validate.ts), so most mistakes show before uploading.
// Returns an error and sets `field` to the field it's about; empty when the form is complete.
juce::String validateForm(const SubmissionForm&, const SubmitInfo&, juce::String& field);

class SubmitClient {
public:
    // fraction 0..1 (or -1 when unknown) and a status line; return false to cancel.
    using Progress = std::function<bool(double fraction, const juce::String& status)>;

    explicit SubmitClient(juce::String baseUrl);   // e.g. https://submit.fueledby.cc
    const juce::String& getBaseUrl() const { return base; }

    SubmitInfo fetchInfo() const;

    // Zips the builds, checks sums, creates the submission, uploads every file in parts and sends it.
    // workDir holds the zips while uploading (removed after). Returns an error, empty on success.
    juce::String submit(const SubmissionForm&, const SubmitInfo&, const juce::File& workDir,
                        SubmissionRecord& out, const Progress& = {}) const;

    juce::String refresh(SubmissionRecord&) const;    // updates status + reviewNote
    juce::String withdraw(SubmissionRecord&) const;

    // The Hub's list of sent submissions (newest first).
    static std::vector<SubmissionRecord> loadRecords(const juce::File& file);
    static void saveRecords(const juce::File& file, const std::vector<SubmissionRecord>&);

    // Zips a picked build into the layout the Hub installs from. Used by submit(); public for tests.
    static juce::String packageBuild(const Build&, const juce::File& destZip);
    static juce::String fileSlug(const juce::String& name);   // "Moss Verb!" -> "mossverb"

private:
    juce::String base;
    struct Reply { int status = 0; juce::var json; juce::String error; };
    Reply request(const juce::String& method, const juce::String& path, const juce::MemoryBlock* body,
                  const juce::String& contentType, const juce::String& token, int timeoutMs = 30000) const;
};

} // namespace hub
