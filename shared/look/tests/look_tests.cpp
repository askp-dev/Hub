// Tests for the fueledby theme engine + a render of every built-in theme (look-*.png).
#include "StudioLook.h"
using namespace lt::ui;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void resetState() { lookState() = LookState(); }

static void renderDemo(const juce::File& out) {
    StudioLook look;
    juce::Image img(juce::Image::ARGB, 1000, 150 * 5 * 2 / 2 * 2, true);
    const int rowH = 150;
    juce::Image page(juce::Image::ARGB, 1040, rowH * (int) themes().size() * 2, true);
    juce::Graphics g(page);
    int row = 0;
    const juce::StringArray ids = [] { juce::StringArray a; for (auto& t : themes()) a.add(t.id); return a; }();
    for (auto& id : ids) for (bool dark : { false, true }) {
        previewTheme(id, dark);
        if (! activeThemeHasBothModes() && dark != activeTheme().dark.has_value()) { continue; }
        look.refreshColours();
        juce::Graphics::ScopedSaveState s(g);
        g.setOrigin(0, row * rowH);
        g.setColour(colours::bg); g.fillRect(0, 0, 1040, rowH);
        drawPanel(g, { 12, 12, 1016, (float) rowH - 24 });
        g.setColour(colours::ink); g.setFont(labelFont(11.0f));
        g.drawText(activeTheme().name.toUpperCase() + (dark ? "  DARK" : "  LIGHT"), 28, 22, 300, 16, juce::Justification::left);
        g.setColour(colours::muted); g.setFont(bodyFont(12.0f));
        g.drawText(juce::String("knob ") + theme::knobName(lookShape().knob) + ", fader " + theme::faderName(lookShape().fader)
                   + ", font " + theme::familyName(lookState().font.family), 28, 40, 400, 16, juce::Justification::left);
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        for (int k = 0; k < 4; ++k) drawKnob(g, { 30.0f + k * 70.0f, 62.0f, 56.0f, 56.0f }, 0.15f + 0.22f * k, a0, a1);
        juce::Slider fader(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
        look.drawLinearSlider(g, 330, 26, 30, 100, 70.0f, 0, 0, juce::Slider::LinearVertical, fader);
        juce::Slider hs(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        look.drawLinearSlider(g, 390, 30, 160, 20, 500.0f, 0, 0, juce::Slider::LinearHorizontal, hs);
        juce::TextButton b1("Bypass"), b2("Solo"); b2.setToggleState(true, juce::dontSendNotification);
        b1.setBounds(0, 0, 80, 28); b2.setBounds(0, 0, 80, 28);
        { juce::Graphics::ScopedSaveState t(g); g.setOrigin(390, 66); look.drawButtonBackground(g, b1, {}, false, false);
          g.setColour(colours::ink); g.setFont(bodyFont(12.0f, true)); g.drawText("Bypass", 0, 0, 80, 28, juce::Justification::centred); }
        { juce::Graphics::ScopedSaveState t(g); g.setOrigin(480, 66); look.drawButtonBackground(g, b2, {}, false, false);
          g.setColour(colours::white); g.setFont(bodyFont(12.0f, true)); g.drawText("Solo", 0, 0, 80, 28, juce::Justification::centred); }
        juce::ToggleButton tg("Link"); tg.setToggleState(true, juce::dontSendNotification); tg.setBounds(0, 0, 90, 20);
        { juce::Graphics::ScopedSaveState t(g); g.setOrigin(390, 104); look.drawToggleButton(g, tg, false, false); }
        // meter
        const juce::Colour st[4] = { colours::accent, colours::mute, colours::solo, colours::alert };
        for (int i = 0; i < 4; ++i) { g.setColour(st[i]); g.fillRoundedRectangle(600.0f + i * 22, 40.0f + i * 12, 12.0f, 80.0f - i * 12, 3.0f); }
        // swatches
        for (int i = 0; i < theme::numTokens; ++i) {
            g.setColour(*theme::tokenTarget(i)); g.fillRoundedRectangle(720.0f + (i % 9) * 32, 34.0f + (i / 9) * 40, 26, 26, 5);
            g.setColour(colours::line); g.drawRoundedRectangle(720.0f + (i % 9) * 32, 34.0f + (i / 9) * 40, 26, 26, 5, 1);
        }
        ++row;
    }
    auto cropped = page.getClippedImage({ 0, 0, 1040, row * rowH });
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(cropped, os);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fueledby-look-test");
    dir.deleteRecursively(); dir.createDirectory();
   #if JUCE_WINDOWS
    _putenv_s("FUELEDBY_LOOK_DIR", dir.getFullPathName().toRawUTF8());
   #else
    setenv("FUELEDBY_LOOK_DIR", dir.getFullPathName().toRawUTF8(), 1);
   #endif

    // 1. built-ins: round trip + contrast
    for (auto& t : theme::builtIns()) {
        auto back = theme::parse(theme::toJson(t));
        CHECK(back.has_value());
        if (! back) continue;
        CHECK(back->id == t.id && back->name == t.name && back->shape.knob == t.shape.knob && back->font.family == t.font.family);
        for (auto* p : { &t.light, &t.dark }) if (*p) {
            const auto& c = (*p)->colour;
            auto col = [&](int i) { return *c[(size_t) i]; };
            const double inkBg = theme::contrast(col(9), col(0)), inkPanel = theme::contrast(col(9), col(2)),
                         mutedPanel = theme::contrast(col(10), col(2)), onInk = theme::contrast(col(12), col(9)),
                         accentPanel = theme::contrast(col(16), col(2));
            std::printf("%-14s ink/bg %.1f ink/panel %.1f muted/panel %.1f onInk/ink %.1f accent/panel %.1f\n",
                        t.id.toRawUTF8(), inkBg, inkPanel, mutedPanel, onInk, accentPanel);
            CHECK(inkBg >= 7.0); CHECK(inkPanel >= 7.0); CHECK(mutedPanel >= 4.5); CHECK(onInk >= 7.0); CHECK(accentPanel >= 3.0);
        }
    }
    // 2. parse rejects
    juce::String err;
    CHECK(! theme::parse("nope", &err));
    CHECK(! theme::parse(R"({"format":1,"id":"Bad Id","name":"x","light":{"ink":"#000000"}})", &err));
    CHECK(! theme::parse(R"({"format":1,"id":"me.x","name":"x"})", &err));
    CHECK(! theme::parse(R"({"format":1,"id":"me.x","name":"x","light":{"ink":"red"}})", &err));
    CHECK(! theme::parse(R"({"format":1,"id":"me.x","name":"x","light":{"ink":"#000000"},"pad":")" + juce::String::repeatedString("a", 17000) + "\"}", &err));
    auto ok = theme::parse(R"({"format":2,"id":"me.x","name":"X","light":{"ink":"#102030"},"shape":{"knob":"dot","corner":99},"future":true})", &err);
    CHECK(ok && ok->light->colour[9] == juce::Colour(0xff102030) && ok->shape.knob == KnobStyle::dot && ok->shape.corner == 14.0f);

    // 3. first run with no settings, migrating the old per-plugin dark mode
    {
        auto legacy = uiSettingsOptions().getDefaultFile();
        legacy.getParentDirectory().createDirectory();
        juce::PropertiesFile pf(legacy, uiSettingsOptions()); pf.setValue("darkMode", true); pf.saveIfNeeded();
        resetState();
        ensureLookLoaded();
        CHECK(darkTheme());
        CHECK(lookSettingsFile().existsAsFile());
        CHECK(activeTheme().id == "fueledby.studio");
        CHECK(colours::bg == juce::Colour(0xff1b1c1e));
        legacy.deleteFile();
    }
    // 4. selecting persists; a fresh load (another plugin, next session) sees it
    selectTheme("fueledby.graphite");
    CHECK(colours::accent == juce::Colour(0xff5b95f5));   // dark graphite
    resetState(); ensureLookLoaded();
    CHECK(activeTheme().id == "fueledby.graphite" && darkTheme());

    // 5. another process changes the file -> poll picks it up
    {
        const int gen = themeGeneration();
        juce::Thread::sleep(1100);   // file times can be 1 s resolution
        lookSettingsFile().replaceWithText(R"({"theme":"fueledby.paper","dark":false,"counter":99})");
        lookState().lastPollMs = 0;
        pollThemeFile();
        CHECK(activeTheme().id == "fueledby.paper" && ! darkTheme() && themeGeneration() > gen);
        CHECK(colours::accent == juce::Colour(0xffb5532a));
    }
    // 6. user themes in the Themes folder; fueledby.* ids and duplicates ignored; bad files skipped
    {
        themesFolder().createDirectory();
        themesFolder().getChildFile("mine.fueledbytheme").replaceWithText(R"({"format":1,"id":"me.mine","name":"Mine","dark":{"bg":"#101010","ink":"#F0F0F0"}})");
        themesFolder().getChildFile("fake.fueledbytheme").replaceWithText(R"({"format":1,"id":"fueledby.studio","name":"Fake","light":{"ink":"#FF0000"}})");
        themesFolder().getChildFile("zz-dupe.fueledbytheme").replaceWithText(R"({"format":1,"id":"me.mine","name":"Dupe","light":{"ink":"#FF0000"}})");
        themesFolder().getChildFile("broken.fueledbytheme").replaceWithText("{");
        scanThemes();
        int mine = 0; for (auto& t : themes()) if (t.id == "me.mine") ++mine;
        CHECK(mine == 1);
        CHECK(findTheme("fueledby.studio")->name == "Studio");
        selectTheme("me.mine");
        CHECK(! activeThemeHasBothModes());
        CHECK(colours::bg == juce::Colour(0xff101010) && colours::panel == juce::Colour(0xff25262a));   // missing -> Studio dark
        CHECK(colours::accent == colours::ink);
        // theme file deleted -> falls back to Studio on next poll
        themesFolder().getChildFile("mine.fueledbytheme").deleteFile(); themesFolder().getChildFile("zz-dupe.fueledbytheme").deleteFile();
        juce::Thread::sleep(1100);
        themesFolder().getChildFile("touch.tmp").create(); themesFolder().getChildFile("touch.tmp").deleteFile();
        lookState().lastPollMs = 0; lookState().folderStamp = {};
        pollThemeFile();
        CHECK(activeTheme().id == "fueledby.studio");
    }
    // 6b. pre-rebrand names: an askp.* id in the settings file, a .askptheme user theme
    {
        juce::Thread::sleep(1100);
        lookSettingsFile().replaceWithText(R"({"theme":"askp.moss","dark":true,"counter":120})");
        lookState().lastPollMs = 0;
        pollThemeFile();
        CHECK(activeTheme().id == "fueledby.moss" && darkTheme());
        CHECK(canonicalThemeId("askp.console") == "fueledby.console" && canonicalThemeId("me.mine") == "me.mine");
        themesFolder().getChildFile("old.askptheme").replaceWithText(R"({"format":1,"id":"me.old","name":"Old","dark":{"bg":"#202020"}})");
        themesFolder().getChildFile("oldfake.askptheme").replaceWithText(R"({"format":1,"id":"askp.paper","name":"Fake","light":{"ink":"#FF0000"}})");
        scanThemes();
        CHECK(findTheme("me.old") != nullptr && findTheme("askp.paper") == nullptr);
        selectTheme("askp.graphite");
        CHECK(activeTheme().id == "fueledby.graphite");
        themesFolder().getChildFile("old.askptheme").deleteFile(); themesFolder().getChildFile("oldfake.askptheme").deleteFile();
        selectTheme("fueledby.studio");
    }
    // 7. fonts load
    CHECK(fonts::Inter::getInstance() != nullptr && fonts::Inter::getInstance()->regular != nullptr);
    CHECK(fonts::Inter::getInstance()->bold != nullptr);

    if (argc > 1) renderDemo(juce::File(juce::String(argv[1])));
    dir.deleteRecursively();
    std::printf(failures ? "%d FAILED\n" : "all look tests passed\n", failures);
    return failures ? 1 : 0;
}
