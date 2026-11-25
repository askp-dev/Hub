// fueledby Hub: one app to install, update and remove fueledby plugins.
//
//   Hub                                   normal window
//   Hub --manifest <url or file>          use another plugin list (testing)
//   Hub --snapshot out.png [--dark] [--busy <id>] [--install <id>]   render the window to a PNG and quit
//   Hub --snapshot out.png --page submit [--example | --sent] [--full]   the "Submit a plugin" page
//   Hub --submit-url <url>                 use another submissions service (testing)
//   Hub --submit-json <file>               send a submission described in a JSON file, print its reference, quit
//
// FUELEDBY_HUB_MANIFEST overrides the list too; FUELEDBY_HUB_ROOT installs into a sandbox folder;
// FUELEDBY_HUB_SUBMIT_URL overrides the submissions service.
#include <juce_gui_basics/juce_gui_basics.h>
#include "HubComponent.h"
#include <iostream>

namespace {
juce::String argAfter(const juce::StringArray& args, const juce::String& flag) {
    const int i = args.indexOf(flag);
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String();
}
} // namespace

class HubApp final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "fueledby Hub"; }
    const juce::String getApplicationVersion() override { return HUB_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override {
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        juce::String source = argAfter(args, "--manifest");
        if (source.isEmpty()) source = hub::Paths::env("HUB_MANIFEST");
        if (source.isEmpty()) source = HUB_MANIFEST_URL;
        source = source.unquoted();
        const auto submitUrl = argAfter(args, "--submit-url").unquoted();

        if (const auto spec = argAfter(args, "--submit-json").unquoted(); spec.isNotEmpty()) {
            setApplicationReturnValue(submitFromJson(juce::File::getCurrentWorkingDirectory().getChildFile(spec), submitUrl));
            quit();
            return;
        }

        if (const auto snap = argAfter(args, "--snapshot").unquoted(); snap.isNotEmpty()) {
            hub::ui::loadThemePreference();   // once only, so it can't undo the line below
            hub::ui::applyTheme(args.contains("--dark"));
            hub::HubComponent c(source, true, submitUrl);
            if (argAfter(args, "--page") == "submit") {
                c.showSubmit(true);
                if (auto* page = c.getSubmitPage()) {
                    page->opened(true);
                    if (args.contains("--example")) page->fillExample();
                    if (args.contains("--sent")) page->showSentExample();
                    if (args.contains("--full")) c.setSize(c.getWidth(), page->contentHeight() + 76 + 40 + 8);   // the whole form, unscrolled
                }
            }
            if (const auto busy = argAfter(args, "--busy"); busy.isNotEmpty())
                if (auto* card = c.card(busy)) card->showDemoProgress(0.46, "Downloading 3.9 MB of 8.4 MB");
            // --install <id>: press Install and wait, to check the whole flow headless
            if (const auto id = argAfter(args, "--install"); id.isNotEmpty())
                if (auto* card = c.card(id)) {
                    card->startInstall();
                    for (int i = 0; i < 600 && card->isBusy(); ++i)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);   // the job's completion callback
                }
            const auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 2.0f);
            const auto out = juce::File::getCurrentWorkingDirectory().getChildFile(snap);
            out.deleteFile();
            juce::FileOutputStream os(out);
            juce::PNGImageFormat().writeImageToStream(img, os);
            setApplicationReturnValue(os.openedOk() ? 0 : 1);
            quit();
            return;
        }
        window = std::make_unique<Window>(source, submitUrl);
    }

    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    // Headless submission, for checking the whole flow against a test service. The JSON holds the form
    // fields by name, plus "mac" / "win" / "screenshots" arrays of paths and "agree": true.
    static int submitFromJson(const juce::File& specFile, juce::String url) {
        const auto spec = juce::JSON::parse(specFile);
        if (! spec.isObject()) { std::cerr << "Can't read " << specFile.getFullPathName() << "\n"; return 2; }
        if (url.isEmpty()) url = hub::Paths::env("HUB_SUBMIT_URL");
        if (url.isEmpty()) url = HUB_SUBMIT_URL;
        hub::SubmitClient client(url);
        const auto info = client.fetchInfo();
        if (! info.reachable) { std::cerr << info.error << "\n"; return 1; }
        hub::SubmissionForm f;
        auto str = [&](const char* k) { return spec.getProperty(k, {}).toString(); };
        f.name = str("name"); f.creator = str("creator"); f.email = str("email"); f.version = str("version");
        f.tagline = str("tagline"); f.description = str("description"); f.category = str("category"); f.sourceUrl = str("sourceUrl");
        f.homepage = str("homepage"); f.license = str("license"); f.licenseOther = str("licenseOther"); f.offer = str("offer");
        f.priceUsd = str("priceUsd"); f.testedIn = str("testedIn"); f.notes = str("notes");
        f.agreeRights = f.agreeSource = f.agreeTerms = (bool) spec.getProperty("agree", false);
        auto files = [&](const char* k) {
            juce::Array<juce::File> out;
            if (auto* a = spec.getProperty(k, {}).getArray())
                for (auto& v : *a) out.add(specFile.getParentDirectory().getChildFile(v.toString()));
            return out;
        };
        for (auto [build, key] : { std::pair<hub::Build*, const char*> { &f.mac, "mac" }, { &f.win, "win" } })
            if (const auto picked = files(key); ! picked.isEmpty())
                if (const auto err = hub::addToBuild(*build, picked); err.isNotEmpty()) { std::cerr << err << "\n"; return 1; }
        f.screenshots = files("screenshots");
        hub::SubmissionRecord rec;
        const auto work = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fueledby-submit");
        juce::String last;
        const auto err = client.submit(f, info, work, rec, [&](double, const juce::String& t) {
            if (t != last) { std::cout << t << "\n"; last = t; }
            return true;
        });
        if (err.isNotEmpty()) { std::cerr << err << "\n"; return 1; }
        std::cout << "Sent " << rec.ref << " (" << rec.id << "): " << rec.status << "\n";
        // A status check right away, the same call the Hub page makes.
        if (const auto e = client.refresh(rec); e.isNotEmpty()) { std::cerr << e << "\n"; return 1; }
        std::cout << "Status: " << hub::statusLabel(rec.status) << "\n";
        return 0;
    }

    class Window final : public juce::DocumentWindow {
    public:
        Window(const juce::String& source, const juce::String& submitUrl)
            : DocumentWindow("fueledby Hub", juce::Colour(0xffececec), DocumentWindow::closeButton | DocumentWindow::minimiseButton) {
            setUsingNativeTitleBar(true);
            setContentOwned(new hub::HubComponent(source, false, submitUrl), true);
            setResizable(true, false);
            setResizeLimits(640, 420, 1600, 1400);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION(HubApp)
