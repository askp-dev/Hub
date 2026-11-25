#pragma once
// The Hub's "Submit a plugin" page: community creators send builds, a source link and their details
// for review. Shown in place of the plugin list (HubComponent::showSubmit).
#include <juce_gui_basics/juce_gui_basics.h>
#include "StudioLook.h"
#include "../core/Installer.h"
#include "../core/Submission.h"

namespace hub {

class SubmitPage final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    SubmitPage(const Paths& paths, juce::String serviceUrl);
    ~SubmitPage() override;

    std::function<void()> onBack;

    void opened(bool sync = false);   // (re)check the service and the status of past submissions
    void fillExample();               // snapshots: a filled-in form
    void showSentExample();           // snapshots: the thank-you state
    int contentHeight() const { return body.getHeight(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return mode == Mode::form && ! busy(); }
    void fileDragEnter(const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragging = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    enum class Mode { form, sent };
    struct Job {
        std::atomic<bool> running { false }, cancel { false };
        std::atomic<double> fraction { 0.0 };
        juce::CriticalSection lock;
        juce::String text;
        void setText(const juce::String& t) { const juce::ScopedLock sl(lock); text = t; }
        juce::String getText() { const juce::ScopedLock sl(lock); return text; }
    };
    struct Caption { juce::Rectangle<int> r; juce::String text; int kind; };   // 0 section, 1 label, 2 hint, 3 value

    class Body final : public juce::Component {
    public:
        explicit Body(SubmitPage& p) : page(p) {}
        void paint(juce::Graphics& g) override { page.paintBody(g); }
    private:
        SubmitPage& page;
    };

    Paths paths;
    SubmitClient client;
    SubmitInfo info;
    bool infoLoaded = false;
    std::vector<SubmissionRecord> records;
    juce::File recordsFile() const { return paths.dataDir.getChildFile("submissions.json"); }

    Mode mode = Mode::form;
    SubmissionRecord sent;
    juce::String sentEmail;
    SubmissionForm form;
    std::shared_ptr<Job> job = std::make_shared<Job>();
    juce::String error, errorField;
    bool dragging = false;

    juce::Viewport viewport;
    Body body { *this };
    std::vector<Caption> captions;
    juce::Rectangle<int> macRow, winRow, shotRow, recordsArea, bannerArea, sentArea;

    juce::TextEditor name, version, tagline, description, homepage, creator, email, sourceUrl, licenseOther, price, testedIn, notes;
    juce::ComboBox category, license, offer;
    juce::TextButton macFiles { "Choose files" }, macFolder { "Choose folder" }, macClear { "Clear" },
                     winFiles { "Choose files" }, winFolder { "Choose folder" }, winClear { "Clear" },
                     shotAdd { "Add" }, shotClear { "Clear" }, submit { "Submit for review" }, cancel { "Cancel" },
                     checkAgain { "Check status" }, another { "Submit another" }, back { "Back to plugins" };
    juce::ToggleButton agreeRights { "I made this plugin, or I have the right to distribute it and its source." },
                       agreeSource { "The source code link is public and builds these binaries." },
                       agreeTerms { "I agree to the fueledby community creator terms." };
    juce::HyperlinkButton terms { "Read the terms", juce::URL("https://fueledby.cc/terms") };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool pool { 1 };   // declared last: stops before the rest goes

    bool busy() const { return job->running.load(); }
    bool canSubmit() const { return infoLoaded && info.reachable && info.open && ! busy(); }
    void paintBody(juce::Graphics&);
    void layoutBody();
    void readForm();
    void applyInfo();
    void choose(Build& build, bool folders);
    void addFiles(Build& build, const juce::Array<juce::File>& files);
    void addScreenshots(const juce::Array<juce::File>& files);
    void startSubmit();
    void refreshRecords();
    void showError(const juce::String& message, const juce::String& field);
    juce::Component* fieldComponent(const juce::String& key);
    void styleEditor(juce::TextEditor&, bool multiLine, const juce::String& placeholder);
    void timerCallback() override { repaint(); body.repaint(); }
};

} // namespace hub
