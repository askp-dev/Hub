#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StudioLook.h"
#include "../core/Installer.h"
#include "SubmitPage.h"

namespace hub {

class HubComponent;

// Progress of one install, shared between a card and the worker running it.
struct JobState {
    std::atomic<bool> running { false }, cancel { false };
    std::atomic<double> fraction { 0.0 };
    juce::CriticalSection lock;
    juce::String text;
    void setText(const juce::String& t) { const juce::ScopedLock sl(lock); text = t; }
    juce::String getText() { const juce::ScopedLock sl(lock); return text; }
};

class PluginCard final : public juce::Component, private juce::Timer {
public:
    PluginCard(HubComponent&, PluginInfo);
    ~PluginCard() override;

    const PluginInfo& info() const { return plugin; }
    void setInfo(PluginInfo p);
    void refreshStatus();
    bool hasUpdate() const { return status.installed && compareVersions(status.version, plugin.version) < 0; }
    bool isBusy() const { return job->running.load(); }
    void startInstall();
    void showDemoProgress(double fraction, const juce::String& text);   // snapshots only

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    HubComponent& owner;
    PluginInfo plugin;
    InstallStatus status;
    std::shared_ptr<JobState> job = std::make_shared<JobState>();
    juce::String message;    // last error or note, shown in the status line
    bool messageIsError = false;

    juce::OwnedArray<juce::TextButton> chips;   // one per format
    juce::TextButton action, remove { "Remove" }, cancel { "Cancel" }, notes { "What's new" };

    std::vector<Format> chosenFormats() const;
    void updateButtons();
    void startUninstall();
    void timerCallback() override;
    juce::String statusLine() const;
};

class HubComponent final : public juce::Component {
public:
    // manifestSource: an https URL or a local file path. syncLoad: read it before
    // returning (snapshots and tests), otherwise in the background.
    // submitUrl: the plugin submissions service (empty: HUB_SUBMIT_URL / FUELEDBY_HUB_SUBMIT_URL).
    HubComponent(juce::String manifestSource, bool syncLoad = false, juce::String submitUrl = {});
    ~HubComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    Installer& getInstaller() { return installer; }
    juce::ThreadPool& getPool() { return pool; }
    void cardsChanged();   // a card finished a job or changed status
    PluginCard* card(const juce::String& id);
    void refresh();
    void showSubmit(bool show);              // the "Submit a plugin" page in place of the list
    SubmitPage* getSubmitPage() { return submitPage.get(); }

private:
    ui::StudioLook look;
    Installer installer;
    juce::String source, submitUrl;
    std::optional<Manifest> manifest;
    juce::String banner;   // offline / error / Hub update note
    bool bannerIsHubUpdate = false, loading = false;

    ui::ThemeButton theme;
    ui::ThemeMenuButton themeMenu;
    ui::ThemeFollower themeFollow { *this, look, [this] { for (auto* c : cards) c->refreshStatus(); } };
    juce::TextButton refreshButton { "Refresh" }, updateAll { "Update all" }, openFolder { "Show folder" },
                     submitButton { "Submit a plugin" };
    std::unique_ptr<SubmitPage> submitPage;
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<PluginCard> cards;
    juce::ThreadPool pool { 1 };   // one install at a time; declared last so it stops before the cards go

    void applyManifest(const juce::String& text, bool fromCache, const juce::String& fetchError);
    static juce::String fetch(const juce::String& source, juce::String& error);
    void mouseUp(const juce::MouseEvent&) override;
    juce::Rectangle<int> bannerArea() const;
};

} // namespace hub
