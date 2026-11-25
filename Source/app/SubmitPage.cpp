#include "SubmitPage.h"

namespace hub {

using namespace ui;

namespace {
constexpr int pad = 24, colGap = 16, labelH = 18, rowH = 32, rowGap = 12, sectionGap = 26;
const juce::String dot = juce::String::fromUTF8("  \xc2\xb7  ");

// Used until the service says otherwise (and when it can't be reached).
const juce::StringArray defaultCategories { "Dynamics", "EQ & filter", "Saturation & distortion", "Reverb & space", "Delay",
                                            "Modulation", "Pitch & vocal", "Amp & cab", "Utility & analysis", "Instrument", "Other" };
const juce::StringArray defaultLicenses { "GPL-3.0", "AGPL-3.0", "LGPL-3.0", "MIT", "BSD-3-Clause", "Apache-2.0", "MPL-2.0", "ISC", "Other" };

void setPrimary(juce::TextButton& b, bool primary) {
    b.setToggleState(primary, juce::dontSendNotification);
    b.setColour(juce::TextButton::textColourOnId, colours::white);
    b.setColour(juce::TextButton::textColourOffId, primary ? colours::ink : colours::muted);
}

void fillCombo(juce::ComboBox& box, const juce::StringArray& items) {
    const auto keep = box.getText();
    box.clear(juce::dontSendNotification);
    for (int i = 0; i < items.size(); ++i) box.addItem(items[i], i + 1);
    if (keep.isNotEmpty() && items.contains(keep)) box.setText(keep, juce::dontSendNotification);
}

juce::String when(juce::int64 ms) {
    return ms > 0 ? juce::Time(ms).formatted("%d %b %Y") : juce::String();
}
} // namespace

SubmitPage::SubmitPage(const Paths& p, juce::String serviceUrl) : paths(p), client(std::move(serviceUrl)) {
    records = SubmitClient::loadRecords(recordsFile());

    styleEditor(name, false, "e.g. Moss Verb");
    styleEditor(version, false, "1.0.0");
    styleEditor(tagline, false, "One line: what it does");
    styleEditor(description, true, "What it does, what makes it different, and anything a reviewer should try first.");
    styleEditor(homepage, false, "https://  (optional)");
    styleEditor(creator, false, "Your name or studio, as shown in the Hub");
    styleEditor(email, false, "you@example.com  (only for review, never shown)");
    styleEditor(sourceUrl, false, "https://github.com/you/your-plugin");
    styleEditor(licenseOther, false, "License name");
    styleEditor(price, false, "USD, optional");
    styleEditor(testedIn, false, "e.g. Ableton Live 12, FL Studio 2024, Reaper 7  (optional)");
    styleEditor(notes, true, "Anything else the reviewer should know  (optional)");
    price.setInputRestrictions(7, "0123456789.$");
    version.setInputRestrictions(40, "0123456789.-+abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

    fillCombo(category, defaultCategories);
    category.setTextWhenNothingSelected("Pick one");
    fillCombo(license, defaultLicenses);
    license.setTextWhenNothingSelected("Pick one");
    license.onChange = [this] { licenseOther.setVisible(license.getText() == "Other"); layoutBody(); };
    for (int i = 0; i < (int) offerChoices().size(); ++i) offer.addItem(offerChoices()[(size_t) i].second, i + 1);
    offer.setTextWhenNothingSelected("Pick one");
    offer.onChange = [this] { layoutBody(); };
    for (auto* c : { &category, &license, &offer }) c->onChange = [this, c, prev = c->onChange] { if (prev) prev(); if (errorField.isNotEmpty()) { error.clear(); errorField.clear(); body.repaint(); } juce::ignoreUnused(c); };

    macFiles.onClick = [this] { choose(form.mac, false); };
    macFolder.onClick = [this] { choose(form.mac, true); };
    macClear.onClick = [this] { form.mac = Build { "mac" }; layoutBody(); };
    winFiles.onClick = [this] { choose(form.win, false); };
    winFolder.onClick = [this] { choose(form.win, true); };
    winClear.onClick = [this] { form.win = Build { "win" }; layoutBody(); };
    shotAdd.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Add screenshots", juce::File(), "*.png;*.jpg;*.jpeg");
        juce::Component::SafePointer<SubmitPage> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::canSelectMultipleItems,
                             [safe](const juce::FileChooser& fc) { if (safe != nullptr) safe->addScreenshots(fc.getResults()); });
    };
    shotClear.onClick = [this] { form.screenshots.clear(); layoutBody(); };
    submit.onClick = [this] { startSubmit(); };
    cancel.onClick = [this] { job->cancel = true; };
    checkAgain.onClick = [this] { refreshRecords(); };
    another.onClick = [this] { mode = Mode::form; layoutBody(); viewport.setViewPosition(0, 0); };
    back.onClick = [this] { if (onBack) onBack(); };
    for (auto* t : { &agreeRights, &agreeSource, &agreeTerms })
        t->onClick = [this] { if (errorField == "agree") { error.clear(); errorField.clear(); body.repaint(); } };
    terms.setFont(bodyFont(12.0f, true), false, juce::Justification::centredLeft);
    terms.setColour(juce::HyperlinkButton::textColourId, colours::ink);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &name, &version, &tagline, &description, &homepage, &creator, &email, &sourceUrl, &licenseOther, &price, &testedIn, &notes,
             &category, &license, &offer, &macFiles, &macFolder, &macClear, &winFiles, &winFolder, &winClear, &shotAdd, &shotClear,
             &submit, &cancel, &checkAgain, &another, &back, &agreeRights, &agreeSource, &agreeTerms, &terms })
        body.addAndMakeVisible(c);
    licenseOther.setVisible(false);

    viewport.setViewedComponent(&body, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);
}

SubmitPage::~SubmitPage() {
    job->cancel = true;
    pool.removeAllJobs(true, 15000);
}

void SubmitPage::styleEditor(juce::TextEditor& e, bool multiLine, const juce::String& placeholder) {
    e.setMultiLine(multiLine, true);
    e.setReturnKeyStartsNewLine(multiLine);
    e.setScrollbarsShown(multiLine);
    e.setFont(bodyFont(13.0f));
    e.setIndents(10, multiLine ? 8 : 8);
    e.setTextToShowWhenEmpty(placeholder, colours::ghost);
    e.onTextChange = [this, &e] {
        if (fieldComponent(errorField) == &e) { error.clear(); errorField.clear(); e.removeColour(juce::TextEditor::outlineColourId); body.repaint(); }
    };
}

juce::Component* SubmitPage::fieldComponent(const juce::String& key) {
    static const char* keys[] = { "name", "version", "tagline", "description", "homepage", "creator", "email", "sourceUrl",
                                  "licenseOther", "priceUsd", "testedIn", "notes", "category", "license", "offer" };
    juce::Component* comps[] = { &name, &version, &tagline, &description, &homepage, &creator, &email, &sourceUrl,
                                 &licenseOther, &price, &testedIn, &notes, &category, &license, &offer };
    for (size_t i = 0; i < std::size(keys); ++i) if (key == keys[i]) return comps[i];
    if (key == "builds") return &macFiles;
    if (key == "screenshots") return &shotAdd;
    if (key == "agree") return &agreeRights;
    return nullptr;
}

// ------------------------------------------------------------------ service

void SubmitPage::opened(bool sync) {
    auto work = [c = client, recs = records]() mutable {
        auto i = c.fetchInfo();
        if (i.reachable) for (auto& r : recs) c.refresh(r);
        return std::make_pair(i, recs);
    };
    if (sync) {
        auto [i, recs] = work();
        info = i; infoLoaded = true; records = recs;
        applyInfo();
        return;
    }
    juce::Component::SafePointer<SubmitPage> safe(this);
    juce::Thread::launch([safe, work]() mutable {
        auto result = work();
        juce::MessageManager::callAsync([safe, result] {
            if (safe == nullptr) return;
            safe->info = result.first;
            safe->infoLoaded = true;
            // keep records added meanwhile; update the ones we checked
            for (auto& checked : result.second)
                for (auto& r : safe->records) if (r.id == checked.id) { r.status = checked.status; r.reviewNote = checked.reviewNote; }
            SubmitClient::saveRecords(safe->recordsFile(), safe->records);
            safe->applyInfo();
        });
    });
}

void SubmitPage::applyInfo() {
    if (info.reachable) {
        if (! info.categories.isEmpty()) fillCombo(category, info.categories);
        if (! info.licenses.isEmpty()) fillCombo(license, info.licenses);
        terms.setURL(juce::URL(info.termsUrl));
    }
    layoutBody();
}

void SubmitPage::refreshRecords() {
    if (records.empty()) return;
    checkAgain.setEnabled(false);
    juce::Component::SafePointer<SubmitPage> safe(this);
    juce::Thread::launch([safe, c = client, recs = records]() mutable {
        for (auto& r : recs) c.refresh(r);
        juce::MessageManager::callAsync([safe, recs] {
            if (safe == nullptr) return;
            for (auto& checked : recs)
                for (auto& r : safe->records) if (r.id == checked.id) { r.status = checked.status; r.reviewNote = checked.reviewNote; }
            SubmitClient::saveRecords(safe->recordsFile(), safe->records);
            safe->checkAgain.setEnabled(true);
            safe->layoutBody();
        });
    });
}

// ------------------------------------------------------------------ picking files

void SubmitPage::choose(Build& build, bool folders) {
    const auto title = juce::String("Choose the ") + (build.platform == "mac" ? "Mac" : "Windows") + " build";
    chooser = std::make_unique<juce::FileChooser>(title, juce::File(), folders ? "*" : "*.vst3;*.component;*.clap;*.zip");
    const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems
                    | (folders ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles);
    juce::Component::SafePointer<SubmitPage> safe(this);
    Build* target = &build;
    chooser->launchAsync(flags, [safe, target](const juce::FileChooser& fc) {
        if (safe != nullptr && ! fc.getResults().isEmpty()) safe->addFiles(*target, fc.getResults());
    });
}

void SubmitPage::addFiles(Build& build, const juce::Array<juce::File>& files) {
    if (auto err = addToBuild(build, files); err.isNotEmpty()) showError(err, "builds");
    else if (errorField == "builds") { error.clear(); errorField.clear(); }
    layoutBody();
}

void SubmitPage::addScreenshots(const juce::Array<juce::File>& files) {
    for (auto& f : files) {
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") { showError(f.getFileName() + " isn't a PNG or JPEG.", "screenshots"); continue; }
        if (form.screenshots.size() >= 4) { showError("Up to 4 screenshots.", "screenshots"); break; }
        form.screenshots.addIfNotAlreadyThere(f);
    }
    layoutBody();
}

void SubmitPage::filesDropped(const juce::StringArray& paths_, int x, int y) {
    dragging = false;
    juce::Array<juce::File> files, images;
    for (auto& p : paths_) {
        const juce::File f(p);
        const auto ext = f.getFileExtension().toLowerCase();
        (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ? images : files).add(f);
    }
    if (! images.isEmpty()) addScreenshots(images);
    if (files.isEmpty()) return;
    const auto at = body.getLocalPoint(this, juce::Point<int>(x, y));
    Build* target = nullptr;
    if (macRow.contains(at)) target = &form.mac;
    else if (winRow.contains(at)) target = &form.win;
    else {   // dropped elsewhere: guess the platform from the first bundle
        bool mac = false;
        for (auto& f : files)
            if (f.getFileExtension().equalsIgnoreCase(".component") || bundleLooksLike(f, "mac")) mac = true;
        target = mac ? &form.mac : &form.win;
    }
    addFiles(*target, files);
}

// ------------------------------------------------------------------ submit

void SubmitPage::readForm() {
    form.name = name.getText(); form.version = version.getText(); form.tagline = tagline.getText();
    form.description = description.getText(); form.homepage = homepage.getText(); form.creator = creator.getText();
    form.email = email.getText(); form.sourceUrl = sourceUrl.getText(); form.licenseOther = licenseOther.getText();
    form.priceUsd = offer.getSelectedId() == 2 ? price.getText() : juce::String();   // a price only makes sense for the Store
    form.testedIn = testedIn.getText(); form.notes = notes.getText();
    form.category = category.getText(); form.license = license.getText();
    const int o = offer.getSelectedId();
    form.offer = o >= 1 && o <= (int) offerChoices().size() ? offerChoices()[(size_t) o - 1].first : juce::String();
    form.agreeRights = agreeRights.getToggleState();
    form.agreeSource = agreeSource.getToggleState();
    form.agreeTerms = agreeTerms.getToggleState();
}

void SubmitPage::showError(const juce::String& message, const juce::String& field) {
    if (auto* old = dynamic_cast<juce::TextEditor*>(fieldComponent(errorField))) old->removeColour(juce::TextEditor::outlineColourId);
    error = message;
    errorField = field;
    if (auto* c = fieldComponent(field)) {
        if (auto* te = dynamic_cast<juce::TextEditor*>(c)) { te->setColour(juce::TextEditor::outlineColourId, colours::alert); te->grabKeyboardFocus(); }
        const int y = c->getY() - 60;
        if (y < viewport.getViewPositionY() || c->getBottom() > viewport.getViewPositionY() + viewport.getViewHeight())
            viewport.setViewPosition(0, juce::jmax(0, y));
    }
    body.repaint();
}

void SubmitPage::startSubmit() {
    if (! canSubmit()) return;
    readForm();
    juce::String field;
    if (auto err = validateForm(form, info, field); err.isNotEmpty()) { showError(err, field); return; }
    error.clear(); errorField.clear();
    job->running = true; job->cancel = false; job->fraction = -1.0;
    job->setText("Getting ready");
    startTimerHz(20);
    layoutBody();

    auto state = job;
    juce::Component::SafePointer<SubmitPage> safe(this);
    const auto workDir = paths.dataDir.getChildFile("Submissions");
    pool.addJob([state, safe, c = client, f = form, i = info, workDir] {
        SubmissionRecord rec;
        const auto err = c.submit(f, i, workDir, rec, [&](double fr, const juce::String& t) {
            state->fraction = fr;
            state->setText(t);
            return ! state->cancel.load() && ! juce::Thread::currentThreadShouldExit();
        });
        state->running = false;
        juce::MessageManager::callAsync([safe, err, rec, email = f.email.trim()] {
            if (safe == nullptr) return;
            safe->stopTimer();
            if (err.isEmpty()) {
                safe->records.insert(safe->records.begin(), rec);
                SubmitClient::saveRecords(safe->recordsFile(), safe->records);
                safe->sent = rec;
                safe->sentEmail = email;
                safe->mode = Mode::sent;
                safe->form = SubmissionForm {};
                for (auto* e : { &safe->name, &safe->version, &safe->tagline, &safe->description, &safe->homepage, &safe->sourceUrl,
                                 &safe->licenseOther, &safe->price, &safe->testedIn, &safe->notes })
                    e->clear();
                for (auto* t : { &safe->agreeRights, &safe->agreeSource, &safe->agreeTerms }) t->setToggleState(false, juce::dontSendNotification);
                safe->viewport.setViewPosition(0, 0);
            } else if (err != "Cancelled") {
                safe->error = err;
                safe->errorField = "submit";
            }
            safe->layoutBody();
        });
    });
}

// ------------------------------------------------------------------ examples for snapshots

void SubmitPage::fillExample() {
    name.setText("Moss Verb"); version.setText("1.0.0"); tagline.setText("A small, warm plate reverb with a freeze switch");
    description.setText("A plate reverb modelled on a 1970s steel plate, with damping, pre-delay and a freeze switch. "
                        "Try the Dark Plate preset on vocals first.");
    creator.setText("Rae Okafor"); email.setText("rae@example.com");
    sourceUrl.setText("https://github.com/raeokafor/moss-verb");
    category.setText("Reverb & space", juce::dontSendNotification);
    license.setText("GPL-3.0", juce::dontSendNotification);
    offer.setSelectedId(2, juce::dontSendNotification);
    price.setText("19");
    testedIn.setText("Ableton Live 12, Reaper 7, Logic Pro 11");
    form.mac.names = { { Format::vst3, "MossVerb.vst3" }, { Format::au, "MossVerb.component" }, { Format::clap, "MossVerb.clap" } };
    form.win.names = { { Format::vst3, "MossVerb.vst3" }, { Format::clap, "MossVerb.clap" } };
    form.screenshots.add(juce::File::getCurrentWorkingDirectory().getChildFile("moss-verb-dark.png"));
    agreeRights.setToggleState(true, juce::dontSendNotification);
    agreeSource.setToggleState(true, juce::dontSendNotification);
    layoutBody();
}

void SubmitPage::showSentExample() {
    sent.ref = "FB-7K3Q2"; sent.name = "Moss Verb"; sent.version = "1.0.0"; sent.status = "pending";
    sentEmail = "rae@example.com";
    mode = Mode::sent;
    layoutBody();
}

// ------------------------------------------------------------------ layout + paint

void SubmitPage::resized() {
    viewport.setBounds(getLocalBounds().withTrimmedLeft(18).withTrimmedRight(10));
    layoutBody();
}

void SubmitPage::layoutBody() {
    captions.clear();
    const int w = juce::jmax(400, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
    const int inner = w - 2 * pad;
    int y = 6;
    auto section = [&](const juce::String& t) { y += sectionGap - 6; captions.push_back({ { pad, y, inner, 16 }, t, 0 }); y += 26; };
    auto hint = [&](const juce::String& t, int lines = 1) { captions.push_back({ { pad, y, inner, 16 * lines }, t, 2 }); y += 16 * lines + 6; };
    auto labelled = [&](juce::Component& c, juce::Rectangle<int> col, const juce::String& label, int h) {
        captions.push_back({ col.withY(y).withHeight(labelH), label, 1 });
        c.setBounds(col.getX(), y + labelH + 2, col.getWidth(), h);
    };
    const auto full = juce::Rectangle<int>(pad, 0, inner, 0);
    const int half = (inner - colGap) / 2;
    const auto left = juce::Rectangle<int>(pad, 0, half, 0), right = juce::Rectangle<int>(pad + half + colGap, 0, inner - half - colGap, 0);
    const auto wideLeft = juce::Rectangle<int>(pad, 0, inner - 160 - colGap, 0), narrowRight = juce::Rectangle<int>(pad + inner - 160, 0, 160, 0);
    auto advance = [&](int h) { y += labelH + 2 + h + rowGap; };

    const bool formMode = mode == Mode::form;
    juce::Array<juce::Component*> formComps { &name, &version, &tagline, &description, &homepage, &creator, &email, &sourceUrl, &price,
        &testedIn, &notes, &category, &license, &offer, &macFiles, &macFolder, &macClear, &winFiles, &winFolder, &winClear, &shotAdd,
        &shotClear, &submit, &agreeRights, &agreeSource, &agreeTerms, &terms };
    for (auto* c : formComps) c->setVisible(formMode);
    licenseOther.setVisible(formMode && license.getText() == "Other");
    another.setVisible(! formMode);
    back.setVisible(! formMode);
    cancel.setVisible(formMode && busy());
    submit.setVisible(formMode && ! busy());

    // title + intro
    captions.push_back({ { pad, y + 8, inner, 26 }, "Submit a plugin", 4 });
    y += 40;
    hint("Community creators can send a plugin to be listed in the fueledby Hub. A person reviews every submission", 1);
    y -= 6;
    hint("before anything is published. We need your builds, a public link to the source code, and an email to reach you.", 1);

    // service state
    bannerArea = {};
    if (! infoLoaded || ! info.reachable || ! info.open) {
        y += 8;
        bannerArea = { pad, y, inner, 34 };
        y += 34 + 4;
    }

    // past submissions
    recordsArea = {};
    checkAgain.setVisible(! records.empty());
    if (! records.empty()) {
        section("Your submissions");
        const int top = y;
        y += (int) juce::jmin<size_t>(records.size(), 6) * 22;
        for (size_t i = 0; i < juce::jmin<size_t>(records.size(), 6); ++i)
            if (records[i].reviewNote.isNotEmpty()) y += 18;
        recordsArea = { pad, top, inner - 120, y - top };
        checkAgain.setBounds(pad + inner - 110, top - 30, 110, 26);
        y += 4;
    }

    if (formMode) {
        section("The plugin");
        labelled(name, wideLeft, "Plugin name", rowH);
        labelled(version, narrowRight, "Version", rowH);
        advance(rowH);
        labelled(tagline, full, "Short description", rowH);
        advance(rowH);
        labelled(description, full, "Description", 96);
        advance(96);
        labelled(category, left, "Category", rowH);
        labelled(homepage, right, "Website", rowH);
        advance(rowH);

        section("You");
        labelled(creator, left, "Creator name", rowH);
        labelled(email, right, "Email", rowH);
        advance(rowH);

        section("Source code and license");
        labelled(sourceUrl, full, "Source code link", rowH);
        advance(rowH);
        labelled(license, left, "License", rowH);
        if (licenseOther.isVisible()) labelled(licenseOther, right, "License name", rowH);
        advance(rowH);

        section("Builds");
        hint("Pick the .vst3, .component and .clap bundles, your build folder (e.g. MyPlugin_artefacts/Release), or a zip with");
        y -= 6;
        hint("VST3/, AU/ and CLAP/ folders inside. You can also drop them on this page. At least one platform is required.");
        y += 4;
        auto buildRow = [&](juce::Rectangle<int>& area, const juce::String& label, const Build& b,
                            juce::TextButton& files, juce::TextButton& folder, juce::TextButton& clear) {
            area = { pad, y, inner, 44 };
            auto r = area.reduced(12, 6);
            clear.setVisible(formMode && ! b.empty());
            if (clear.isVisible()) { clear.setBounds(r.removeFromRight(64).withSizeKeepingCentre(64, 28)); r.removeFromRight(8); }
            folder.setBounds(r.removeFromRight(112).withSizeKeepingCentre(112, 28)); r.removeFromRight(8);
            files.setBounds(r.removeFromRight(104).withSizeKeepingCentre(104, 28));
            captions.push_back({ { pad + 12, y, 120, 44 }, label, 1 });
            captions.push_back({ { pad + 132, y, r.getRight() - pad - 140, 44 }, b.empty() ? juce::String("Nothing yet") : b.describe(), b.empty() ? 2 : 3 });
            y += 44 + 8;
        };
        buildRow(macRow, "Mac build", form.mac, macFiles, macFolder, macClear);
        buildRow(winRow, "Windows build", form.win, winFiles, winFolder, winClear);
        {
            shotRow = { pad, y, inner, 44 };
            auto r = shotRow.reduced(12, 6);
            shotClear.setVisible(! form.screenshots.isEmpty());
            if (shotClear.isVisible()) { shotClear.setBounds(r.removeFromRight(64).withSizeKeepingCentre(64, 28)); r.removeFromRight(8); }
            shotAdd.setBounds(r.removeFromRight(104).withSizeKeepingCentre(104, 28));
            juce::StringArray names;
            for (auto& f : form.screenshots) names.add(f.getFileName());
            captions.push_back({ { pad + 12, y, 120, 44 }, "Screenshots", 1 });
            captions.push_back({ { pad + 132, y, r.getRight() - pad - 140, 44 },
                                 names.isEmpty() ? juce::String("Optional, up to 4 PNG or JPEG") : names.joinIntoString(", "), names.isEmpty() ? 2 : 3 });
            y += 44 + 12;
        }
        labelled(testedIn, full, "Tested in", rowH);
        advance(rowH);

        section("How you want to offer it");
        const bool store = offer.getSelectedId() == 2;
        price.setVisible(store);
        labelled(offer, store ? wideLeft : full, "Offer", rowH);
        if (store) labelled(price, narrowRight, "Price idea", rowH);
        advance(rowH);
        y -= 4;
        hint("Plugins sold in the Store or included in the subscription pay you a share as a community creator. The terms have the split.");

        section("Anything else");
        labelled(notes, full, "Notes for the reviewer", 72);
        advance(72);

        section("Before you send");
        for (auto* t : { &agreeRights, &agreeSource, &agreeTerms }) {
            t->setBounds(pad, y, juce::jmin(inner, 620), 26);
            y += 30;
        }
        terms.setBounds(pad - 2, y, 120, 22);
        y += 34;

        // submit row: error or progress on the left, button on the right
        submit.setBounds(pad + inner - 170, y, 170, 36);
        cancel.setBounds(submit.getBounds());
        submit.setEnabled(canSubmit());
        setPrimary(submit, submit.isEnabled());
        y += 36 + pad;
    } else {
        section("Sent");
        sentArea = { pad, y, inner, 118 };
        y += 118 + 14;
        another.setBounds(pad + inner - 150 - 8 - 140, y, 150, 34);
        back.setBounds(pad + inner - 140, y, 140, 34);
        setPrimary(back, true);
        y += 34 + pad;
    }

    body.setSize(w, y);
    body.repaint();
}

void SubmitPage::paint(juce::Graphics& g) {
    if (dragging) {
        g.setColour(colours::ink.withAlpha(0.6f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(6.0f), 12.0f, 2.0f);
    }
}

void SubmitPage::paintBody(juce::Graphics& g) {
    // build + screenshot rows sit on a panel, like the plugin cards
    if (mode == Mode::form)
        for (auto r : { macRow, winRow, shotRow }) drawPanel(g, r.toFloat(), 9.0f);

    for (auto& c : captions) {
        switch (c.kind) {
            case 4: g.setColour(colours::ink); g.setFont(bodyFont(20.0f, true)); break;
            case 0: g.setColour(colours::ink); g.setFont(labelFont(10.5f)); break;
            case 1: g.setColour(colours::muted); g.setFont(labelFont(9.5f)); break;
            case 2: g.setColour(colours::muted); g.setFont(bodyFont(12.0f)); break;
            default: g.setColour(colours::ink); g.setFont(bodyFont(12.5f, true)); break;
        }
        const auto text = (c.kind == 0 || c.kind == 1) ? c.text.toUpperCase() : c.text;
        g.drawFittedText(text, c.r, juce::Justification::centredLeft, c.r.getHeight() > 20 ? 2 : 1, 0.95f);
        if (c.kind == 0) {   // a hairline after section titles
            const int tw = g.getCurrentFont().getStringWidth(text) + 12;
            g.setColour(colours::line);
            g.fillRect(c.r.getX() + tw, c.r.getCentreY(), c.r.getWidth() - tw, 1);
        }
    }

    if (! bannerArea.isEmpty()) {
        g.setColour(colours::surface);
        g.fillRoundedRectangle(bannerArea.toFloat(), 8.0f);
        g.setColour(colours::line);
        g.drawRoundedRectangle(bannerArea.toFloat().reduced(0.5f), 8.0f, 1.0f);
        g.setColour(colours::ink);
        g.setFont(bodyFont(12.0f));
        const auto msg = ! infoLoaded ? juce::String("Checking the submissions service...")
                       : ! info.reachable ? "Submissions can't be sent right now: " + info.error
                       : juce::String("Plugin submissions are closed right now. Please check back soon.");
        g.drawText(msg, bannerArea.reduced(14, 0), juce::Justification::centredLeft, true);
    }

    if (! recordsArea.isEmpty()) {
        int y = recordsArea.getY();
        for (size_t i = 0; i < juce::jmin<size_t>(records.size(), 6); ++i) {
            const auto& r = records[i];
            auto line = juce::Rectangle<int>(recordsArea.getX(), y, recordsArea.getWidth(), 22);
            g.setColour(colours::muted);
            g.setFont(labelFont(9.5f));
            g.drawText(r.ref, line.removeFromLeft(80), juce::Justification::centredLeft);
            g.setColour(colours::ink);
            g.setFont(bodyFont(12.5f, true));
            const auto title = r.name + " " + r.version;
            const int tw = juce::jmin(line.getWidth() / 2, g.getCurrentFont().getStringWidth(title) + 4);
            g.drawText(title, line.removeFromLeft(tw), juce::Justification::centredLeft, true);
            const bool attention = r.status == "changes_requested" || r.status == "rejected";
            g.setColour(attention ? colours::alert : colours::muted);
            g.setFont(bodyFont(12.0f));
            g.drawText(dot.trimStart() + statusLabel(r.status) + (r.submittedAt > 0 ? dot + "sent " + when(r.submittedAt) : juce::String()),
                       line, juce::Justification::centredLeft, true);
            y += 22;
            if (r.reviewNote.isNotEmpty()) {
                g.setColour(colours::ink);
                g.setFont(bodyFont(12.0f));
                g.drawText(juce::String::fromUTF8("\xe2\x80\x9c") + r.reviewNote + juce::String::fromUTF8("\xe2\x80\x9d"),
                           juce::Rectangle<int>(recordsArea.getX() + 80, y - 2, recordsArea.getWidth() - 80, 18), juce::Justification::centredLeft, true);
                y += 18;
            }
        }
    }

    if (mode == Mode::sent && ! sentArea.isEmpty()) {
        drawPanel(g, sentArea.toFloat(), 12.0f);
        auto r = sentArea.reduced(22, 18);
        g.setColour(colours::ink);
        g.setFont(bodyFont(17.0f, true));
        g.drawText("Thanks! " + sent.name + " " + sent.version + " is waiting for review.", r.removeFromTop(24), juce::Justification::centredLeft, true);
        g.setColour(colours::muted);
        g.setFont(labelFont(9.5f));
        g.drawText(("Reference " + sent.ref).toUpperCase(), r.removeFromTop(20), juce::Justification::centredLeft);
        g.setColour(colours::ink);
        g.setFont(bodyFont(12.5f));
        g.drawFittedText("A person on the fueledby team will try your builds and read the source. We'll email " + sentEmail
                         + " if we have questions, and when it's decided. Nothing is published until then. The status shows at the top of this page.",
                         r.reduced(0, 4), juce::Justification::topLeft, 3);
    }

    if (mode == Mode::form) {
        const auto row = submit.getBounds().withX(pad).withRight(submit.getX() - 16);
        if (busy()) {
            const double f = job->fraction.load();
            auto tr = row.toFloat().withSizeKeepingCentre(row.getWidth(), 4.0f).translated(0.0f, 9.0f);
            g.setColour(colours::track);
            g.fillRoundedRectangle(tr, 2.0f);
            g.setColour(colours::ink);
            if (f < 0.0) {
                const double t = std::fmod(juce::Time::getMillisecondCounterHiRes() / 900.0, 1.0);
                const float bw = tr.getWidth() * 0.25f;
                g.fillRoundedRectangle(tr.withX(tr.getX() + (float) t * (tr.getWidth() - bw)).withWidth(bw), 2.0f);
            } else {
                g.fillRoundedRectangle(tr.withWidth(tr.getWidth() * (float) juce::jlimit(0.0, 1.0, f)), 2.0f);
            }
            g.setColour(colours::muted);
            g.setFont(labelFont(9.5f));
            g.drawText(job->getText().toUpperCase(), row.withTrimmedBottom(14), juce::Justification::centredLeft, true);
        } else if (error.isNotEmpty()) {
            g.setColour(colours::alert);
            g.setFont(bodyFont(12.5f, true));
            g.drawFittedText(error, row, juce::Justification::centredLeft, 2);
        }
    }
}

} // namespace hub
