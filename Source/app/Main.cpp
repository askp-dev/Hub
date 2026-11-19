// askp Hub: one app to install, update and remove askp plugins.
//
//   Hub                                   normal window
//   Hub --manifest <url or file>          use another plugin list (testing)
//   Hub --snapshot out.png [--dark] [--busy <id>] [--install <id>]   render the window to a PNG and quit
//
// ASKP_HUB_MANIFEST overrides the list too; ASKP_HUB_ROOT installs into a sandbox folder.
#include <juce_gui_basics/juce_gui_basics.h>
#include "HubComponent.h"

namespace {
juce::String argAfter(const juce::StringArray& args, const juce::String& flag) {
    const int i = args.indexOf(flag);
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String();
}
} // namespace

class HubApp final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "askp Hub"; }
    const juce::String getApplicationVersion() override { return HUB_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override {
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        juce::String source = argAfter(args, "--manifest");
        if (source.isEmpty()) source = juce::SystemStats::getEnvironmentVariable("ASKP_HUB_MANIFEST", HUB_MANIFEST_URL);
        source = source.unquoted();

        if (const auto snap = argAfter(args, "--snapshot").unquoted(); snap.isNotEmpty()) {
            hub::ui::loadThemePreference();   // once only, so it can't undo the line below
            hub::ui::applyTheme(args.contains("--dark"));
            hub::HubComponent c(source, true);
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
        window = std::make_unique<Window>(source);
    }

    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class Window final : public juce::DocumentWindow {
    public:
        explicit Window(const juce::String& source)
            : DocumentWindow("askp Hub", juce::Colour(0xffececec), DocumentWindow::closeButton | DocumentWindow::minimiseButton) {
            setUsingNativeTitleBar(true);
            setContentOwned(new hub::HubComponent(source), true);
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
