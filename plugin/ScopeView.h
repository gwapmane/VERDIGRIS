// VERDIGRIS - the tall window in the middle of the pedal. By default it is a little oscilloscope that shows what the
// circuit is doing to your guitar: on top the transfer curve (what goes in against what comes out - a straight line when
// clean, a squashed S when it is fuzzing), in the middle the waveform (dim = your guitar, bright = the fuzz), and along
// the bottom the supply voltage, the Chop pattern and the Swell envelope. A video, GIF or picture of your own can take its
// place - it then brightens and flashes with the sound, with a lighter version of the trace and the status strip on top.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "PluginProcessor.h"
#include "VideoLoop.h"

class ScopeView : public juce::Component, private juce::Timer {
public:
    static constexpr float kW = 216.0f, kH = 396.0f; // design size (the window is drawn at this size, then scaled)

    explicit ScopeView(VerdigrisProcessor& p);
    ~ScopeView() override { stopTimer(); }

    void setLook(juce::Colour accent, juce::Colour deep);
    void setClip(VideoClipPtr clip);
    void paint(juce::Graphics& g) override;

    float level() const { return level_; }   // how loud the output is, 0..1 (smoothed)
    float clipping() const { return clipAct_; } // how hard the circuit is hitting its rails, 0..1 (smoothed)

    // for the test harness: freeze the animation clock
    void setStill(bool still) { still_ = still; }

private:
    static constexpr int kCols = 216;
    void timerCallback() override;
    void readMeters();
    void readScope();
    void drawBackdrop(juce::Graphics& g) const;
    void drawTransfer(juce::Graphics& g) const;
    void drawScope(juce::Graphics& g, bool overVideo) const;
    void drawStatus(juce::Graphics& g, bool overVideo) const;
    void drawGlassOverlay(juce::Graphics& g) const;

    VerdigrisProcessor& proc_;
    juce::Colour accent_{0xffff5b3a}, deep_{0xff05070a};
    VideoClipPtr clip_;
    double clipStart_ = 0.0;
    bool still_ = false;
    double now_ = 0.0;

    // smoothed, 0..1
    float level_ = 0.0f, inLevel_ = 0.0f, clipAct_ = 0.0f, flash_ = 0.0f;
    float supply_ = 9.0f, swell_ = 1.0f, chop_ = 1.0f, beatPulse_ = 0.0f;
    int chopStep_ = 0;
    float outScale_ = 1.0f, inScale_ = 1.0f;

    // what the scope shows (kCols columns): input and output, min / max
    float inMin_[kCols] = {}, inMax_[kCols] = {}, outMin_[kCols] = {}, outMax_[kCols] = {};
    float xyIn_[kCols] = {}, xyOut_[kCols] = {};
    std::vector<juce::Point<float>> prevXy_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeView)
};
