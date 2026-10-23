// VERDIGRIS - the pedal: the artwork, drawn at whatever size the window is, and every control on it.
//
//   knobs         drag up/down (or left/right) - hold Shift for fine control - double-click to reset, mouse wheel works too
//   side levers   click = flip it. Tap and it stays; HOLD (a third of a second) and it only stays while you hold it
//   toggles       TYPE: click to flip.  VOICING: click above / below the lever to move it a step
//   footswitch    tap = fuzz on / off.  HOLD = momentary (the opposite, only while held)
//   A / LIVE / B  click the left / middle / right of the small lever
//
// All the art is drawn at "design size" (the pedal is 560 x 960) and scaled; hardware sprites are pre-shrunk to the
// exact pixel size on screen, so it stays sharp at every window size and on high-DPI screens.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "Assets.h"
#include "PluginProcessor.h"
#include "ScopeView.h"
#include "Settings.h"

class PedalView : public juce::Component, private juce::Timer {
public:
    explicit PedalView(VerdigrisProcessor& p);
    ~PedalView() override { stopTimer(); }

    // design size of the whole view (the pedal plus a margin for the jacks and the floor)
    static constexpr float kDesignW = 648.0f, kDesignH = 1020.0f;
    static constexpr float kPedalX = 44.0f, kPedalY = 20.0f;

    void setScale(float scale); // logical pixels per design pixel; also resizes the view
    float scale() const { return scale_; }
    void setLook(const LookSettings& look, const juce::File& photoFile);
    void setClip(VideoClipPtr clip) { scope_->setClip(std::move(clip)); }
    ScopeView& scope() { return *scope_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    // Rotary: 0 FUZZ, 1 BIAS, 2 VOLUME (big), 3..8 the small knobs under the side levers.  Side: the six levers.
    // Select: 0 TYPE, 1 VOICING, 2 the A / LIVE / B lever.  Foot: the one footswitch.
    struct Hit { enum Kind { None, Rotary, Side, Select, Foot } kind = None; int index = 0; };

    void timerCallback() override;
    Hit hitTest(juce::Point<float> designPos) const;
    void showMidiMenu(Hit hit, juce::Point<int> where);
    juce::Point<float> toPedal(juce::Point<float> logicalPos) const;

    // state helpers
    float rotValue(int i) const;       // 0..1
    juce::String rotText(int i) const; // what the knob is set to, in words
    bool sideOn(int s) const;
    float sideActivity(int s) const;   // 0..1: how hard the effect is working right now (for its glow)
    int selectIndex(int t) const;      // the parameter's value (t == 2: the slot, 0..2)
    int selectPos(int t) const;        // where the lever sits, 0 = up .. 2 = down
    void setRot(int i, float v01);
    void setSelectPos(int t, int pos);
    void setSide(int s, bool on);
    void setFoot(bool on);
    juce::RangedAudioParameter* param(const char* id) const { return proc_.apvts.getParameter(id); }
    bool boolParam(const char* id) const { return proc_.apvts.getRawParameterValue(id)->load() > 0.5f; }

    // drawing
    void ensureCaches(double phys);
    void rebuildStatic(double u);
    void rebuildThemeSprites(double u);
    void paintStatic(juce::Graphics& g, double u);
    void paintDynamic(juce::Graphics& g);
    void paintWordmark(juce::Graphics& g);
    void paintVolumeScale(juce::Graphics& g) const;
    void drawTracked(juce::Graphics& g, const juce::String& text, float cx, float cy, const juce::Font& font, float tracking) const;
    void blit(juce::Graphics& g, int sprite, float ax, float ay) const;
    void blitImage(juce::Graphics& g, const juce::Image& img, float x, float y) const;
    juce::Colour tint(juce::Colour original) const; // re-colour one of the original wine-red light colours
    juce::Rectangle<float> wordmarkArea() const;

    VerdigrisProcessor& proc_;
    std::unique_ptr<ScopeView> scope_;

    float scale_ = 0.72f;
    LookSettings look_;
    juce::File photoFile_;

    juce::Image bodyFull_;    // the coloured body at 1120 x 1920
    bool bodyDirty_ = true;
    bool themeDirty_ = true;
    double u_ = 0.0;          // physical pixels per design pixel the caches were made for
    bool light_ = false;      // the body is light: printed text is drawn in dark ink
    juce::Colour ink_{0xffe9e6e1};
    assets::Sprites sprites_;
    juce::Image bodyScaled_, staticLayer_;
    juce::Image dotLit_, dotOff_, ledOn_, ledOff_;
    juce::Path wordPath_;

    // interaction
    Hit drag_;
    int hoverRot_ = -1;
    float dragValue_ = 0.0f;
    juce::Point<float> lastDragPos_;
    float wheelAccum_ = 0.0f;
    bool footPressed_ = false;
    bool footPrev_ = false;
    std::array<bool, 6> sidePrev_{};   // what a side lever was before it was pressed (a hold goes back to it)
    std::array<double, 6> sideDownAt_{};
    double footDownAt_ = 0.0;

    // what was drawn last time (so the timer only repaints on a real change)
    std::array<float, 48> shown_{};
    std::array<int, 6> glowKey_{};
    int wordKey_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalView)
};
