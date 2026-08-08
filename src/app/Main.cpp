// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.
//
// ⚠️ macOS-only, NEVER COMPILED as of this commit. See KNOWN_ISSUES.md §1.

#include "MainComponent.h"

#include "core/Logging.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace {

/// Mirror structured logs into a file next to the app's data, so a failed rehearsal can
/// be diagnosed after the fact rather than from memory.
void installFileLogSink() {
    static juce::File log_file =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Follow/logs")
            .getChildFile("follow-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")
                          + ".log");
    log_file.getParentDirectory().createDirectory();

    follow::core::Logger::instance().setSink([](const std::string& line) {
        std::fprintf(stderr, "%s\n", line.c_str());
        log_file.appendText(juce::String(line) + "\n");
    });
}

} // namespace

class FollowApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Follow"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        installFileLogSink();
        follow::core::Logger::instance().info(follow::core::LogCategory::System,
                                              "Follow starting",
                                              {{"version", "0.1.0"}});
        main_window_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override {
        follow::core::Logger::instance().info(follow::core::LogCategory::System,
                                              "Follow shutting down");
        main_window_ = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colour(0xff0e0f11),
                             DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new follow::app::MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> main_window_;
};

START_JUCE_APPLICATION(FollowApplication)
