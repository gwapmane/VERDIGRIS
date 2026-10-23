#include "PluginEditor.h"

float VerdigrisEditor::scaleForSize(int size) {
    static const float s[3] = {0.58f, 0.72f, 0.88f};
    return s[juce::jlimit(0, 2, size)];
}

VerdigrisEditor::VerdigrisEditor(VerdigrisProcessor& p)
    : juce::AudioProcessorEditor(p), proc_(p), pedal_(p), bar_(p), panel_(p), save_(p) {
    setLookAndFeel(&laf_);
    addAndMakeVisible(pedal_);
    addAndMakeVisible(bar_);
    addChildComponent(panel_);
    addChildComponent(save_);
    addChildComponent(reveal_);
    addChildComponent(banner_);
    banner_.onClick = [this] { proc_.cancelMidiLearn(); };
    lastLearned_ = proc_.learnedCounter.load();
    startTimerHz(8);

    bar_.onSave = [this] { save_.open(); };
    bar_.onSettings = [this] { openSettings(!settingsOpen_); };
    bar_.onHide = [this] { setPedalOnly(true); };
    reveal_.onClick = [this] { setPedalOnly(false); };
    bar_.onSize = [this](int s) {
        auto l = settings_->look();
        l.size = s;
        settings_->setLook(l);
    };
    panel_.onClose = [this] { openSettings(false); };
    panel_.onVideoChosen = [this](const juce::File& f) {
        auto l = settings_->look();
        l.videoPath = f.getFullPathName();
        settings_->setLook(l);
    };
    panel_.onVideoRemoved = [this] {
        auto l = settings_->look();
        l.videoPath = {};
        settings_->setLook(l);
    };

    settings_->addListener(this);
    applyLook();
    applySize();
    syncVideo(true);
}

VerdigrisEditor::~VerdigrisEditor() {
    stopTimer();
    proc_.cancelMidiLearn();
    settings_->removeListener(this);
    loader_.cancel();
    setLookAndFeel(nullptr);
}

void VerdigrisEditor::timerCallback() {
    const auto now = juce::Time::getMillisecondCounter();
    if (auto* target = proc_.midiLearnTarget()) {
        lastLearned_ = proc_.learnedCounter.load();
        banner_.setText("MIDI learn: move a knob or button on your controller to control " + target->getName(40) + "   (click here to cancel)", true,
                        settings_->look().accent);
        banner_.setVisible(true);
        bannerHideAt_ = 0;
    } else if (const int c = proc_.learnedCounter.load(); c != lastLearned_) {
        lastLearned_ = c;
        if (auto* p = proc_.learnedParam.load())
            banner_.setText(p->getName(40) + " is now controlled by MIDI CC " + juce::String(proc_.learnedCc.load()), false, settings_->look().accent);
        banner_.setVisible(true);
        bannerHideAt_ = static_cast<juce::int64>(now) + 3000;
    } else if (banner_.isVisible() && bannerHideAt_ != 0 && static_cast<juce::int64>(now) > bannerHideAt_) {
        banner_.setVisible(false);
        bannerHideAt_ = 0;
    } else if (banner_.isVisible() && bannerHideAt_ == 0) {
        banner_.setVisible(false); // learning was cancelled
    }
}

void VerdigrisEditor::lookChanged() {
    applyLook();
    applySize();
    syncVideo(false);
}

void VerdigrisEditor::applyLook() {
    const auto& look = settings_->look();
    pedal_.setLook(look, settings_->photoPath());
    bar_.setAccent(look.accent);
    if (look.accent != shownAccent_) {
        shownAccent_ = look.accent;
        laf_.setAccent(look.accent);
        sendLookAndFeelChange();
    }
}

void VerdigrisEditor::applySize() {
    const auto s = scaleForSize(settings_->look().size);
    if (std::abs(pedal_.scale() - s) > 1.0e-4f || pedal_.getWidth() == 0) pedal_.setScale(s);
    bar_.setSize_(settings_->look().size);
    updateWindowSize();
}

void VerdigrisEditor::setPedalOnly(bool only) {
    auto l = settings_->look();
    l.pedalOnly = only;
    if (only) settingsOpen_ = false;
    settings_->setLook(l);
    updateWindowSize();
}

void VerdigrisEditor::updateWindowSize() {
    const bool only = settings_->look().pedalOnly;
    if (only) settingsOpen_ = false;
    panel_.setVisible(settingsOpen_);
    bar_.setVisible(!only);
    reveal_.setVisible(only);
    if (only) save_.setVisible(false);
    const int w = pedal_.getWidth() + (settingsOpen_ ? SettingsPanel::kWidth : 0);
    const int h = (only ? 0 : kBarHeight) + pedal_.getHeight();
    if (w != getWidth() || h != getHeight()) setSize(w, h);
    else resized();
}

void VerdigrisEditor::openSettings(bool open) {
    settingsOpen_ = open && !settings_->look().pedalOnly;
    updateWindowSize();
}

void VerdigrisEditor::syncVideo(bool atStartup) {
    const auto path = settings_->look().videoPath;
    if (path == loadedVideo_) return;
    loadedVideo_ = path;
    loader_.cancel();
    if (path.isEmpty()) {
        pedal_.setClip(nullptr);
        panel_.setVideoStatus("Nothing chosen: the scope shows what the fuzz is doing.", false);
        return;
    }
    const juce::File f(path);
    if (!f.existsAsFile()) {
        pedal_.setClip(nullptr);
        panel_.setVideoStatus("Could not find " + f.getFileName() + " (it may have been moved). Choose it again.", true);
        return;
    }
    panel_.setVideoStatus("Loading " + f.getFileName() + "...", false);
    juce::Component::SafePointer<VerdigrisEditor> safe(this);
    loader_.load(f, [safe, path, f, atStartup](VideoClipPtr clip, juce::String error) {
        if (safe == nullptr || safe->loadedVideo_ != path) return;
        if (clip == nullptr || clip->frames.empty()) {
            safe->pedal_.setClip(nullptr);
            if (!atStartup) { // forget a file that cannot be read, so it is not retried every time the window opens
                auto l = safe->settings_->look();
                l.videoPath = {};
                safe->settings_->setLook(l);
            }
            safe->panel_.setVideoStatus(error.isNotEmpty() ? error : "That video could not be read.", true);
            return;
        }
        safe->pedal_.setClip(clip);
        safe->panel_.setVideoStatus("Playing " + f.getFileName() + " (" + juce::String(clip->duration, 1) + " s, loops).", false);
    });
}

void VerdigrisEditor::paint(juce::Graphics& g) { g.fillAll(ui::bg); }

void VerdigrisEditor::resized() {
    const int pw = pedal_.getWidth();
    const int top = settings_->look().pedalOnly ? 0 : kBarHeight;
    bar_.setBounds(0, 0, pw, kBarHeight);
    pedal_.setTopLeftPosition(0, top);
    reveal_.setBounds(4, 4, 22, 22);
    reveal_.toFront(false);
    banner_.setBounds(juce::Rectangle<int>(0, 0, juce::jmin(pw - 16, 420), 40).withCentre({pw / 2, getHeight() - 40}));
    banner_.toFront(false);
    panel_.setBounds(pw, 0, SettingsPanel::kWidth, getHeight());
    save_.setBounds(0, 0, pw, getHeight());
}
