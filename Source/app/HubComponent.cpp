#include "HubComponent.h"

namespace hub {

using namespace ui;

namespace {
constexpr int cardHeight = 116, headerHeight = 76, footerHeight = 40, bannerHeight = 34, gap = 14;

// " · " as UTF-8 (a plain char* literal is read as ASCII)
const juce::String sep = juce::String::fromUTF8("  \xc2\xb7  ");

// A filled ink button for the main action. StudioLook fills toggled-on buttons with ink.
void setPrimary(juce::TextButton& b, bool primary) {
    b.setToggleState(primary, juce::dontSendNotification);
    b.setColour(juce::TextButton::textColourOnId, colours::white);
    b.setColour(juce::TextButton::textColourOffId, primary ? colours::ink : colours::muted);
}

void styleChip(juce::TextButton& b) {
    b.setClickingTogglesState(true);
    b.setColour(juce::TextButton::buttonOnColourId, colours::ink);
}

// Thin progress track, drawn like the V/A horizontal sliders.
void drawProgress(juce::Graphics& g, juce::Rectangle<float> r, double fraction) {
    const auto tr = r.withSizeKeepingCentre(r.getWidth(), 4.0f);
    g.setColour(colours::track);
    g.fillRoundedRectangle(tr, 2.0f);
    g.setColour(colours::ink);
    if (fraction < 0.0) {   // unknown size: a sliding segment
        const double t = std::fmod(juce::Time::getMillisecondCounterHiRes() / 900.0, 1.0);
        const float w = tr.getWidth() * 0.25f;
        g.fillRoundedRectangle(tr.withX(tr.getX() + (float) t * (tr.getWidth() - w)).withWidth(w), 2.0f);
    } else {
        g.fillRoundedRectangle(tr.withWidth(tr.getWidth() * (float) juce::jlimit(0.0, 1.0, fraction)), 2.0f);
    }
}
} // namespace

// =================================================================== card

PluginCard::PluginCard(HubComponent& o, PluginInfo p) : owner(o), plugin(std::move(p)) {
    for (auto f : allFormats()) {
        auto* b = chips.add(new juce::TextButton(formatLabel(f)));
        styleChip(*b);
        b->setComponentID(formatKey(f));
        b->onClick = [this] { updateButtons(); };
        addAndMakeVisible(b);
    }
    action.onClick = [this] { startInstall(); };
    remove.onClick = [this] { startUninstall(); };
    cancel.onClick = [this] { job->cancel = true; };
    notes.onClick = [this] {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::NoIcon, plugin.name + " " + plugin.version,
                                               plugin.notes.isNotEmpty() ? plugin.notes : juce::String("No notes for this version."));
    };
    for (auto* b : { &action, &remove, &cancel, &notes }) addAndMakeVisible(b);
    refreshStatus();
}

PluginCard::~PluginCard() { job->cancel = true; }

void PluginCard::setInfo(PluginInfo p) { plugin = std::move(p); refreshStatus(); }

void PluginCard::refreshStatus() {
    status = owner.getInstaller().status(plugin);
    // chips start on what's installed, or every format for a fresh install
    const auto avail = plugin.availableFormats();
    for (auto* b : chips) {
        const auto f = *formatFromKey(b->getComponentID());
        const bool offered = std::find(avail.begin(), avail.end(), f) != avail.end();
        b->setVisible(offered);
        const bool on = status.installed ? std::find(status.formats.begin(), status.formats.end(), f) != status.formats.end() : offered;
        b->setToggleState(on, juce::dontSendNotification);
    }
    updateButtons();
}

std::vector<Format> PluginCard::chosenFormats() const {
    std::vector<Format> out;
    for (auto* b : chips) if (b->isVisible() && b->getToggleState()) out.push_back(*formatFromKey(b->getComponentID()));
    return out;
}

void PluginCard::updateButtons() {
    const bool busy = isBusy();
    const auto chosen = chosenFormats();
    const bool formatsChanged = status.installed && chosen != status.formats;
    const bool available = plugin.download() != nullptr;

    juce::String label = "Install";
    bool enabled = available && ! chosen.empty();
    if (! available) label = "Coming soon";
    else if (hasUpdate()) label = "Update";
    else if (status.installed && ! status.managed && status.version.isEmpty()) label = "Reinstall";
    else if (formatsChanged) label = "Apply";
    else if (status.installed) { label = "Installed"; enabled = false; }
    action.setButtonText(label);
    action.setEnabled(enabled && ! busy);
    setPrimary(action, action.isEnabled());

    action.setVisible(! busy);
    remove.setVisible(! busy && status.installed);
    notes.setVisible(! busy && plugin.notes.isNotEmpty());
    cancel.setVisible(busy);
    for (auto* b : chips) b->setEnabled(! busy);
    resized();
    repaint();
}

juce::String PluginCard::statusLine() const {
    if (isBusy()) return job->getText();
    if (message.isNotEmpty()) return message;
    juce::String s;
    const auto* dl = plugin.download();
    if (! status.installed)
        s = "Not installed" + (dl != nullptr && dl->size > 0 ? sep + juce::File::descriptionOfSizeInBytes(dl->size) : juce::String());
    else if (hasUpdate())
        s = "Installed " + status.version + sep + plugin.version + " available";
    else
        s = "Installed " + (status.version.isNotEmpty() ? status.version : juce::String("(version unknown)"));
    if (! status.elsewhere.isEmpty()) s << sep << "older copy in " << juce::File(status.elsewhere[0]).getParentDirectory().getFullPathName();
    return s;
}

void PluginCard::startInstall() {
    if (isBusy()) return;
    message.clear();
    job->running = true; job->cancel = false; job->fraction = 0.0;
    job->setText("Waiting");
    updateButtons();
    startTimerHz(30);
    auto state = job;
    auto info = plugin;
    auto formats = chosenFormats();
    auto& installer = owner.getInstaller();
    juce::Component::SafePointer<PluginCard> safe(this);
    owner.getPool().addJob([state, info, formats, &installer, safe] {
        const auto err = installer.install(info, formats, [&](double f, const juce::String& t) {
            if (f >= 0.0) state->fraction = f; else state->fraction = -1.0;
            state->setText(t);
            return ! state->cancel.load() && ! juce::Thread::currentThreadShouldExit();
        });
        state->running = false;
        juce::MessageManager::callAsync([safe, err] {
            if (safe == nullptr) return;
            safe->stopTimer();
            safe->message = err;
            safe->messageIsError = err.isNotEmpty() && err != "Cancelled";
            safe->refreshStatus();
            safe->owner.cardsChanged();
        });
    });
}

void PluginCard::startUninstall() {
    const auto where = status.elsewhere.isEmpty() ? juce::String()
        : "\n\nAn older copy outside the Hub stays:\n" + status.elsewhere.joinIntoString("\n")
          + "\nDelete it yourself if you no longer want it.";
    juce::Component::SafePointer<PluginCard> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::NoIcon, "Remove " + plugin.name + "?",
        "This removes " + plugin.name + " from your plug-in folders. Projects that use it will open without it." + where,
        "Remove", "Cancel", nullptr,
        juce::ModalCallbackFunction::create([safe](int result) {
            if (safe == nullptr || result != 1) return;
            safe->message = safe->owner.getInstaller().uninstall(safe->plugin);
            safe->messageIsError = safe->message.isNotEmpty();
            safe->refreshStatus();
            safe->owner.cardsChanged();
        }));
}

void PluginCard::showDemoProgress(double fraction, const juce::String& text) {
    job->running = true; job->fraction = fraction; job->setText(text);
    updateButtons();
}

void PluginCard::timerCallback() { repaint(); }

void PluginCard::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.0f, 2.0f);
    drawPanel(g, r, 12.0f);
    auto inner = r.reduced(18.0f, 16.0f);

    // badge tile, like the V/A logo tile
    auto tile = inner.removeFromLeft(62.0f).withSizeKeepingCentre(62.0f, 62.0f);
    g.setColour(colours::ink);
    g.fillRoundedRectangle(tile, 12.0f);
    g.setColour(colours::white);
    g.setFont(bodyFont(plugin.badge.length() > 3 ? 13.0f : 17.0f, true));
    g.drawText(plugin.badge, tile, juce::Justification::centred);
    inner.removeFromLeft(18.0f);

    int textRight = action.getX();
    for (auto* c : chips) if (c->isVisible()) textRight = juce::jmin(textRight, c->getX());
    auto text = inner.withRight((float) textRight - 16.0f);
    g.setColour(colours::ink);
    g.setFont(bodyFont(17.0f, true));
    g.drawText(plugin.name, text.removeFromTop(24.0f), juce::Justification::centredLeft, true);
    g.setColour(colours::muted);
    g.setFont(bodyFont(12.0f));
    g.drawFittedText(plugin.tagline, text.removeFromTop(34.0f).toNearestInt(), juce::Justification::topLeft, 2);

    auto line = text.removeFromBottom(16.0f);
    if (isBusy()) {
        drawProgress(g, line.removeFromBottom(4.0f).withTrimmedRight(4.0f), job->fraction.load());
        line = text.removeFromBottom(14.0f);
    }
    g.setColour(messageIsError && ! isBusy() && message.isNotEmpty() ? colours::alert
                : (hasUpdate() && ! isBusy() ? colours::ink : colours::muted));
    g.setFont(labelFont(9.5f));
    g.drawText(statusLine().toUpperCase(), line, juce::Justification::centredLeft, true);
}

void PluginCard::resized() {
    auto r = getLocalBounds().reduced(19, 18);
    auto right = r.removeFromRight(330);
    auto top = right.removeFromTop(28);
    for (int i = chips.size(); --i >= 0;) {
        if (! chips[i]->isVisible()) continue;
        chips[i]->setBounds(top.removeFromRight(56));
        top.removeFromRight(6);
    }
    auto bottom = right.removeFromBottom(34);
    action.setBounds(bottom.removeFromRight(112));
    cancel.setBounds(action.getBounds());
    bottom.removeFromRight(8);
    if (remove.isVisible()) { remove.setBounds(bottom.removeFromRight(84)); bottom.removeFromRight(8); }
    notes.setBounds(bottom.removeFromRight(104));
}

// =================================================================== hub

HubComponent::HubComponent(juce::String manifestSource, bool syncLoad)
    : installer(Paths::defaults()), source(std::move(manifestSource)) {
    setLookAndFeel(&look);
    theme.onClick = [this] {
        saveThemePreference(! darkTheme());
        look.refreshColours();
        for (auto* c : cards) c->refreshStatus();
        sendLookAndFeelChange();
        repaint();
    };
    refreshButton.onClick = [this] { refresh(); };
    updateAll.onClick = [this] { for (auto* c : cards) if (c->hasUpdate()) c->startInstall(); };
    openFolder.onClick = [this] {
        const auto dir = installer.getPaths().pluginDirs.begin()->second.getParentDirectory();
        dir.createDirectory();
        dir.revealToUser();
    };
    for (juce::Component* c : { (juce::Component*) &theme, (juce::Component*) &refreshButton, (juce::Component*) &updateAll, (juce::Component*) &openFolder })
        addAndMakeVisible(c);
    viewport.setViewedComponent(&list, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);

    // show the last good list straight away, then fetch the current one
    if (const auto cached = installer.readCache("manifest-cache.json"); cached.isNotEmpty())
        applyManifest(cached, true, {});
    if (syncLoad) {
        juce::String err;
        const auto text = fetch(source, err);
        applyManifest(text, false, err);
    } else {
        refresh();
    }
    setSize(860, 640);
}

HubComponent::~HubComponent() {
    pool.removeAllJobs(true, 10000);
    setLookAndFeel(nullptr);
}

juce::String HubComponent::fetch(const juce::String& src, juce::String& error) {
    if (! src.startsWithIgnoreCase("http")) {
        const juce::File f = juce::File::isAbsolutePath(src) ? juce::File(src) : juce::File::getCurrentWorkingDirectory().getChildFile(src);
        if (! f.existsAsFile()) { error = "Can't find the plugin list at " + f.getFullPathName(); return {}; }
        return f.loadFileAsString();
    }
    int code = 0;
    // a query string defeats stale CDN caches after a release
    auto url = juce::URL(src).withParameter("t", juce::String(juce::Time::currentTimeMillis() / 60000));
    auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                        .withConnectionTimeoutMs(15000).withNumRedirectsToFollow(5).withStatusCode(&code)
                                        .withExtraHeaders("User-Agent: askp-hub\r\nCache-Control: no-cache\r\n"));
    if (in == nullptr || code >= 400) {
        error = "Couldn't check for updates" + (code >= 400 ? " (HTTP " + juce::String(code) + ")" : juce::String(", you look offline"));
        return {};
    }
    return in->readEntireStreamAsString();
}

void HubComponent::refresh() {
    for (auto* c : cards) if (c->isBusy()) return;   // the list stays put while installing
    loading = true;
    refreshButton.setEnabled(false);
    repaint();
    juce::Component::SafePointer<HubComponent> safe(this);
    juce::Thread::launch([safe, src = source] {
        juce::String err;
        auto text = fetch(src, err);
        juce::MessageManager::callAsync([safe, text, err] {
            if (safe != nullptr) safe->applyManifest(text, false, err);
        });
    });
}

void HubComponent::applyManifest(const juce::String& text, bool fromCache, const juce::String& fetchError) {
    loading = false;
    refreshButton.setEnabled(true);
    banner.clear();
    bannerIsHubUpdate = false;
    if (fetchError.isNotEmpty()) {
        banner = manifest ? fetchError + ". Showing the list from your last check." : fetchError + ".";
    } else {
        juce::String err;
        if (auto m = Manifest::parse(text, err)) {
            manifest = std::move(m);
            if (! fromCache) installer.writeCache("manifest-cache.json", text);
            if (manifest->hubVersion.isNotEmpty() && compareVersions(HUB_VERSION, manifest->hubVersion) < 0) {
                banner = "Hub " + manifest->hubVersion + " is available. Click here to download it.";
                bannerIsHubUpdate = true;
            }
        } else if (! fromCache) {
            banner = err;
        }
    }

    if (manifest) {
        // keep cards (and any running job) for plugins still listed; add new ones in order
        juce::OwnedArray<PluginCard> next;
        for (auto& p : manifest->plugins) {
            PluginCard* c = nullptr;
            for (int i = 0; i < cards.size(); ++i)
                if (cards[i]->info().id == p.id) { c = cards.removeAndReturn(i); break; }
            if (c != nullptr) c->setInfo(p); else c = new PluginCard(*this, p);
            next.add(c);
        }
        cards.swapWith(next);   // leftovers (plugins dropped from the list) are deleted with `next`
        for (auto* c : cards) list.addAndMakeVisible(c);
    }
    cardsChanged();
    resized();
}

PluginCard* HubComponent::card(const juce::String& id) {
    for (auto* c : cards) if (c->info().id == id) return c;
    return nullptr;
}

void HubComponent::cardsChanged() {
    int updates = 0;
    for (auto* c : cards) if (c->hasUpdate() && ! c->isBusy()) ++updates;
    updateAll.setVisible(updates > 1);
    updateAll.setButtonText("Update all (" + juce::String(updates) + ")");
    setPrimary(updateAll, true);
    resized();
    repaint();
}

juce::Rectangle<int> HubComponent::bannerArea() const {
    if (banner.isEmpty()) return {};
    return getLocalBounds().reduced(24, 0).withY(headerHeight).withHeight(bannerHeight);
}

void HubComponent::mouseUp(const juce::MouseEvent& e) {
    if (bannerIsHubUpdate && bannerArea().contains(e.getPosition()) && manifest)
        if (auto it = manifest->hubDownloads.find(platformKey()); it != manifest->hubDownloads.end())
            juce::URL(it->second).launchInDefaultBrowser();
}

void HubComponent::paint(juce::Graphics& g) {
    g.fillAll(colours::bg);

    // header: logo tile, name, count
    auto header = getLocalBounds().removeFromTop(headerHeight).reduced(24, 18).toFloat();
    auto tile = header.removeFromLeft(40.0f).withSizeKeepingCentre(40.0f, 40.0f);
    g.setColour(colours::ink);
    g.fillRoundedRectangle(tile, 9.0f);
    g.setColour(colours::white);
    g.setFont(bodyFont(12.0f, true));
    g.drawText("HUB", tile, juce::Justification::centred);
    header.removeFromLeft(14.0f);
    g.setColour(colours::ink);
    g.setFont(bodyFont(18.0f, true));
    g.drawText("askp Hub", header.removeFromTop(22.0f), juce::Justification::bottomLeft);
    g.setColour(colours::muted);
    g.setFont(labelFont(9.5f));
    juce::String sub = loading ? "Checking for updates" : (manifest ? juce::String(manifest->plugins.size()) + " plugins" : juce::String("No plugin list yet"));
    g.drawText(sub.toUpperCase(), header, juce::Justification::centredLeft);

    if (auto b = bannerArea(); ! b.isEmpty()) {
        g.setColour(bannerIsHubUpdate ? colours::ink : colours::surface);
        g.fillRoundedRectangle(b.toFloat(), 8.0f);
        if (! bannerIsHubUpdate) { g.setColour(colours::line); g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 8.0f, 1.0f); }
        g.setColour(bannerIsHubUpdate ? colours::white : colours::ink);
        g.setFont(bodyFont(12.0f, bannerIsHubUpdate));
        g.drawText(banner, b.reduced(14, 0), juce::Justification::centredLeft, true);
    }

    if (cards.isEmpty()) {
        g.setColour(colours::muted);
        g.setFont(bodyFont(13.0f));
        g.drawText(loading ? "Loading the plugin list..." : "No plugins to show. Check your connection and press Refresh.",
                   viewport.getBounds(), juce::Justification::centred);
    }

    // footer: where things go, Hub version
    auto footer = getLocalBounds().removeFromBottom(footerHeight);
    g.setColour(colours::footer);
    g.fillRect(footer);
    g.setColour(colours::line);
    g.fillRect(footer.removeFromTop(1));
    footer = footer.reduced(24, 0);
    g.setColour(colours::muted);
    g.setFont(labelFont(9.0f));
    g.drawText("Hub " + juce::String(HUB_VERSION), footer.removeFromRight(120), juce::Justification::centredRight);
    const auto dirs = installer.getPaths().pluginDirs;
    const auto dir = dirs.begin()->second.getParentDirectory();
    const auto homeDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const auto where = dir.isAChildOf(homeDir) ? "~/" + dir.getRelativePathFrom(homeDir) : dir.getFullPathName();
    g.drawText("Installs to " + where, footer.withTrimmedLeft(110), juce::Justification::centredLeft, true);
}

void HubComponent::resized() {
    auto r = getLocalBounds();
    auto header = r.removeFromTop(headerHeight).reduced(24, 20);
    theme.setBounds(header.removeFromRight(36));
    header.removeFromRight(8);
    refreshButton.setBounds(header.removeFromRight(90));
    header.removeFromRight(8);
    if (updateAll.isVisible()) updateAll.setBounds(header.removeFromRight(130));
    if (banner.isNotEmpty()) r.removeFromTop(bannerHeight + 8);
    auto footer = r.removeFromBottom(footerHeight);
    openFolder.setBounds(footer.reduced(24, 7).removeFromLeft(96));
    viewport.setBounds(r.withTrimmedLeft(18).withTrimmedRight(10).withTrimmedBottom(6));

    const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 4;
    list.setSize(w, juce::jmax(1, cards.size() * (cardHeight + gap) + 6));
    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setBounds(6, 4 + i * (cardHeight + gap), w - 8, cardHeight);
}

} // namespace hub
