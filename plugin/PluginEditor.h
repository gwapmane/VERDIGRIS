#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "Chrome.h"
#include "Panels.h"
#include "PedalView.h"
#include "PluginProcessor.h"
#include "Settings.h"
#include "VideoLoop.h"

// The plugin window: a slim bar on top (presets, size, settings) with the pedal below it. The settings panel
// opens on the right (the window grows to make room, so the pedal stays visible while you change its colours).
class VerdigrisEditor : public juce::AudioProcessorEditor, private Settings::Listener, private juce::Timer {
public:
    explicit VerdigrisEditor(VerdigrisProcessor& p);
    ~VerdigrisEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int kBarHeight = 40;
    static float scaleForSize(int size); // 0 small, 1 medium, 2 large

private:
    void lookChanged() override;
    void timerCallback() override; // MIDI learn message
    void applyLook();
    void applySize();
    void updateWindowSize();
    void openSettings(bool open);
    void setPedalOnly(bool only);
    void syncVideo(bool atStartup);

    VerdigrisProcessor& proc_;
    juce::SharedResourcePointer<Settings> settings_;
    VerdigrisLookAndFeel laf_;
    PedalView pedal_;
    PresetBar bar_;
    SettingsPanel panel_;
    SavePanel save_;
    RevealHandle reveal_;
    MidiBanner banner_;
    int lastLearned_ = 0;
    juce::int64 bannerHideAt_ = 0;
    VideoLoader loader_;
    bool settingsOpen_ = false;
    juce::Colour shownAccent_{0x00000000};
    juce::String loadedVideo_{"?"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerdigrisEditor)
};
