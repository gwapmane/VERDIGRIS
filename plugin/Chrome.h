// VERDIGRIS - the plugin's "chrome": the look of its buttons, sliders and menus (matching the pedal: dark wine,
// silver, Jost), the slim preset bar along the top, and a few small icon buttons.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "PluginProcessor.h"

namespace ui {
inline const juce::Colour bg{0xff130d10}, panel{0xff1b1317}, panelHi{0xff241820}, line{0xff3a2a30}, lineHi{0xff6b4652},
    text{0xffd8dadd}, dim{0xff9a9ea3}, silver{0xffd0d4d8}, ink{0xff1a1316};
}

class VerdigrisLookAndFeel : public juce::LookAndFeel_V4 {
public:
    VerdigrisLookAndFeel();
    void setAccent(juce::Colour accent) { accent_ = accent; }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Font getLabelFont(juce::Label&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }

private:
    juce::Colour accent_{0xffE8405E};
};

// a small icon on a button: previous / next / settings
class IconButton : public juce::Button {
public:
    enum Icon { Prev, Next, Settings, Hide };
    IconButton(const juce::String& name, Icon icon) : juce::Button(name), icon_(icon) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    Icon icon_;
};

// the preset selector: a recessed box with the preset's name (and a one-line hint), a dot when it was changed
class NameButton : public juce::Button {
public:
    NameButton() : juce::Button("presets") {}
    void set(const juce::String& name, const juce::String& hint, bool modified);
    void setAccent(juce::Colour c) { accent_ = c; repaint(); }
    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    juce::String name_, hint_;
    bool modified_ = false;
    juce::Colour accent_{0xffE8405E};
};

// The little message that appears while a MIDI controller is being learned (click it to cancel).
class MidiBanner : public juce::Component {
public:
    std::function<void()> onClick;
    void setText(const juce::String& t, bool learning, juce::Colour accent);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    juce::String text_;
    bool learning_ = false;
    juce::Colour accent_{0xffE8405E};
};

// A small round handle in the corner of the pedal that brings the bar back when only the pedal is showing.
class RevealHandle : public juce::Component {
public:
    RevealHandle() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override { over_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { over_ = false; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && contains(e.getPosition()) && onClick) onClick(); }

private:
    bool over_ = false;
};

// The strip along the top of the window: < preset name > Save | settings | size
class PresetBar : public juce::Component, private juce::Timer {
public:
    explicit PresetBar(VerdigrisProcessor& p);
    ~PresetBar() override { stopTimer(); }

    std::function<void()> onSave, onSettings, onHide;
    std::function<void(int)> onSize;
    void setSize_(int size); // which size letter to show (0, 1, 2)
    void setAccent(juce::Colour c) { accent_ = c; nameButton_.setAccent(c); repaint(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPresetMenu();
    void showSizeMenu();

    VerdigrisProcessor& proc_;
    IconButton prev_{"prev", IconButton::Prev}, next_{"next", IconButton::Next}, gear_{"settings", IconButton::Settings},
        hide_{"hide", IconButton::Hide};
    NameButton nameButton_;
    juce::TextButton save_{"Save"}, size_{"M"};
    juce::String shownName_, shownHint_;
    bool shownModified_ = false;
    juce::Colour accent_{0xffE8405E};
};
