#pragma once
// "Studio white": the V/A look, shared by the Hub app. Monochrome soft-white panels, thin black
// arcs, vertical faders with tick marks. Everything is drawn as vectors so the
// window scales cleanly.
#include <juce_gui_basics/juce_gui_basics.h>

namespace hub::ui {

// Colours are tokens, not literals: applyTheme() swaps the whole palette (light
// or dark) and every paint() reads the current value. "white" is the colour that
// sits on ink (knob caps, text on a filled button), so it flips with the theme.
namespace colours {
    // light values until applyTheme() runs
    inline juce::Colour bg { 0xffececec }, footer { 0xffe1e1e1 }, panel { 0xfff7f7f7 }, surface { 0xfffbfbfb },
                        line { 0xffdcdcdc }, grid { 0xffe6e6e6 }, track { 0xffe2e2e2 }, tick { 0xffc4c4c4 },
                        rim { 0xffd2d2d2 }, ink { 0xff1b1b1b }, muted { 0xff6b6b6b }, ghost { 0xff9a9a9a },
                        white { 0xffffffff };
    inline juce::Colour mute    { 0xffd9822b };   // slot muted
    inline juce::Colour solo    { 0xffd8b52a };   // slot soloed
    inline juce::Colour alert   { 0xffe0433a };   // clip / over
    inline float shadow = 0.06f;                  // panel drop-shadow strength
}

inline bool& darkTheme() { static bool dark = false; return dark; }

inline void applyTheme(bool dark) {
    using namespace colours;
    darkTheme() = dark;
    if (!dark) {
        bg = juce::Colour(0xffececec); footer = juce::Colour(0xffe1e1e1); panel = juce::Colour(0xfff7f7f7);
        surface = juce::Colour(0xfffbfbfb); line = juce::Colour(0xffdcdcdc); grid = juce::Colour(0xffe6e6e6);
        track = juce::Colour(0xffe2e2e2); tick = juce::Colour(0xffc4c4c4); rim = colours::rim;
        ink = juce::Colour(0xff1b1b1b); muted = juce::Colour(0xff6b6b6b); ghost = juce::Colour(0xff9a9a9a);
        white = juce::Colour(0xffffffff); shadow = 0.06f;
    } else {
        bg = juce::Colour(0xff1b1c1e); footer = juce::Colour(0xff151618); panel = juce::Colour(0xff25262a);
        surface = juce::Colour(0xff2b2c30); line = juce::Colour(0xff393a3f); grid = juce::Colour(0xff303135);
        track = juce::Colour(0xff3a3b40); tick = juce::Colour(0xff56575d); rim = juce::Colour(0xff4a4b51);
        ink = juce::Colour(0xffe9e9ea); muted = juce::Colour(0xff9c9da2); ghost = juce::Colour(0xff6f7076);
        white = juce::Colour(0xff1e1f22); shadow = 0.30f;
    }
}

// The theme is a per-user UI preference (not part of a session), kept next to the presets.
inline juce::PropertiesFile& uiSettings() {
    static juce::PropertiesFile file([] {
        juce::PropertiesFile::Options o;
        o.applicationName = "UI"; o.filenameSuffix = "settings"; o.folderName = "askp Hub";
        o.osxLibrarySubFolder = "Application Support";
        return o; }());
    return file;
}
inline void loadThemePreference() {
    static const bool once = [] { applyTheme(uiSettings().getBoolValue("darkMode", false)); return true; }();
    juce::ignoreUnused(once);
}
inline void saveThemePreference(bool dark) {
    applyTheme(dark);
    uiSettings().setValue("darkMode", dark);
    uiSettings().saveIfNeeded();
}

inline juce::Font labelFont(float size = 10.0f) {
    return juce::Font(juce::FontOptions(size, juce::Font::bold)).withExtraKerningFactor(0.12f);
}
inline juce::Font bodyFont(float size = 12.0f, bool bold = false) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}

// Rounded white panel with a hairline border and a soft shadow.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.0f) {
    juce::DropShadow(juce::Colours::black.withAlpha(colours::shadow), 6, { 0, 2 }).drawForPath(g, [&] {
        juce::Path p; p.addRoundedRectangle(r, radius); return p; }());
    g.setColour(colours::panel);
    g.fillRoundedRectangle(r, radius);
    g.setColour(colours::line);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

class StudioLook final : public juce::LookAndFeel_V4 {
public:
    StudioLook() { loadThemePreference(); refreshColours(); }

    // Re-read the palette into JUCE's colour ids (after applyTheme()).
    void refreshColours() {
        using namespace colours;
        setColour(juce::ResizableWindow::backgroundColourId, bg);
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::backgroundColourId, white);
        setColour(juce::ComboBox::outlineColourId, line);
        setColour(juce::ComboBox::textColourId, ink);
        setColour(juce::ComboBox::arrowColourId, ink);
        setColour(juce::PopupMenu::backgroundColourId, white);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, ink);
        setColour(juce::PopupMenu::highlightedTextColourId, white);
        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, ink);
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, white);
        setColour(juce::TooltipWindow::backgroundColourId, panel);
        setColour(juce::TooltipWindow::textColourId, ink);
        setColour(juce::TooltipWindow::outlineColourId, line);
        setColour(juce::ScrollBar::thumbColourId, tick);
        setColour(juce::Label::textColourId, ink);
        setColour(juce::TextEditor::backgroundColourId, white);
        setColour(juce::TextEditor::textColourId, ink);
        setColour(juce::TextEditor::outlineColourId, line);
        setColour(juce::TextEditor::highlightColourId, ink.withAlpha(0.25f));
        setColour(juce::CaretComponent::caretColourId, ink);
        setColour(juce::AlertWindow::backgroundColourId, panel);
        setColour(juce::AlertWindow::textColourId, ink);
        setColour(juce::AlertWindow::outlineColourId, line);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float start, float end, juce::Slider&) override {
        const auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3.0f);
        const float size = juce::jmin(r.getWidth(), r.getHeight());
        const auto c = r.getCentre();
        const float radius = size * 0.5f - 2.0f;

        juce::Path arc;
        arc.addCentredArc(c.x, c.y, radius, radius, 0.0f, start, start + pos * (end - start), true);
        g.setColour(colours::ink);
        g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float cap = radius * 0.72f;
        g.setColour(colours::white);
        g.fillEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c));
        g.setColour(colours::rim);
        g.drawEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c), 1.0f);

        const float angle = start + pos * (end - start);
        juce::Path pointer;
        pointer.startNewSubPath(c);
        pointer.lineTo(c.getPointOnCircumference(cap * 0.8f, angle));
        g.setColour(colours::ink);
        g.strokePath(pointer, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& s) override {
        if (style == juce::Slider::LinearHorizontal) {   // thin track, ink fill, round thumb
            const float cy = (float) y + (float) h * 0.5f;
            const auto tr = juce::Rectangle<float>((float) x, cy - 2.0f, (float) w, 4.0f);
            g.setColour(colours::track);
            g.fillRoundedRectangle(tr, 2.0f);
            g.setColour(s.isEnabled() ? colours::ink : colours::ghost);
            g.fillRoundedRectangle(tr.withRight(sliderPos), 2.0f);
            g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ sliderPos, cy }));
            return;
        }
        if (style != juce::Slider::LinearVertical) {
            LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, 0, 0, style, s);
            return;
        }
        const float cx = (float) x + (float) w * 0.5f - 5.0f;
        const auto trackR = juce::Rectangle<float>(cx - 3.0f, (float) y, 6.0f, (float) h);
        g.setColour(colours::track);
        g.fillRoundedRectangle(trackR, 3.0f);
        g.setColour(colours::ink);
        g.fillRoundedRectangle(trackR.withTop(sliderPos), 3.0f);
        // tick marks
        g.setColour(colours::tick);
        for (float ty = (float) y; ty <= (float) (y + h); ty += 8.0f)
            g.fillRect(cx + 7.0f, ty, 6.0f, 1.0f);
        // handle
        g.setColour(colours::ink);
        g.fillRoundedRectangle(juce::Rectangle<float>(16.0f, 6.0f).withCentre({ cx, sliderPos }), 3.0f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = b.getToggleState();
        g.setColour(on ? colours::ink : (down ? colours::track : (over ? colours::white : colours::panel)));
        g.fillRoundedRectangle(r, 7.0f);
        if (!on) { g.setColour(colours::line); g.drawRoundedRectangle(r, 7.0f, 1.0f); }
    }

    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return bodyFont(juce::jmin(12.0f, (float) h * 0.5f), true); }

    // Toggle buttons are drawn as small pill switches with their label on the left.
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool) override {
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        auto sw = r.removeFromRight(28.0f).withSizeKeepingCentre(28.0f, 16.0f);
        g.setColour(on ? colours::ink : colours::rim);
        g.fillRoundedRectangle(sw, 8.0f);
        g.setColour(colours::white);
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ on ? sw.getRight() - 8.0f : sw.getX() + 8.0f, sw.getCentreY() }));
        g.setColour(b.isEnabled() ? colours::ink : colours::muted);
        g.setFont(bodyFont(11.0f));
        g.drawText(b.getButtonText(), r, juce::Justification::centredLeft);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override {
        const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
        g.setColour(colours::white);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(colours::line);
        g.drawRoundedRectangle(r, 7.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.startNewSubPath(ax - 4, ay - 2); arrow.lineTo(ax, ay + 2); arrow.lineTo(ax + 4, ay - 2);
        g.setColour(colours::ink);
        g.strokePath(arrow, juce::PathStrokeType(1.5f));
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return bodyFont(12.0f, true); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(8, 0, box.getWidth() - 26, box.getHeight());
        label.setFont(getComboBoxFont(box));
    }
};

// Header button that flips light/dark: a circle, half filled.
class ThemeButton final : public juce::Button {
public:
    ThemeButton() : juce::Button("Theme") {}
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? colours::track : (over ? colours::white : colours::panel));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(colours::line);
        g.drawRoundedRectangle(r, 6.0f, 1.0f);
        const auto c = r.withSizeKeepingCentre(14.0f, 14.0f);
        g.setColour(colours::ink);
        g.drawEllipse(c, 1.5f);
        juce::Path half;
        half.addPieSegment(c, 0.0f, juce::MathConstants<float>::pi, 0.0f);
        g.fillPath(half);
    }
};

} // namespace hub::ui
