// VERDIGRIS - the two panels that slide in over the pedal:
//   SavePanel      "Save as a preset": a name box (already filled in, so you can just press Save)
//   SettingsPanel  the side panel with two tabs:
//                    LOOK   body colour, lights colour, texture, your own picture, the window video / picture, window size
//                    SOUND  the options that live under the knobs: side switches, tempo, under-the-hood, ramp, trims, MIDI, ...
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "Chrome.h"
#include "PluginProcessor.h"
#include "Settings.h"

// A simple top-to-bottom layout: section headings and rows of components, laid out for a given width.
class FlowPage : public juce::Component {
public:
    void addHeader(const juce::String& title);
    void addRow(std::vector<juce::Component*> comps, int height, int gapAbove = 8);
    int layoutFor(int width); // positions everything, returns the total height
    void paint(juce::Graphics& g) override;

private:
    struct Row {
        juce::String header;
        std::vector<juce::Component*> comps;
        int h = 0, gap = 0, y = 0;
    };
    std::vector<Row> rows_;
};

// a row of colour dots plus a rainbow "pick any colour" dot at the end
class SwatchRow : public juce::Component {
public:
    explicit SwatchRow(std::vector<juce::Colour> colours) : colours_(std::move(colours)) {}
    std::function<void(juce::Colour)> onPick;
    std::function<void(juce::Rectangle<int> tile)> onCustom;
    void setSelected(juce::Colour c) { selected_ = c; repaint(); }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent&) override { hover_ = -1; repaint(); }

private:
    float pitch() const { return juce::jmin(38.0f, getWidth() / static_cast<float>(colours_.size() + 1)); }
    juce::Rectangle<int> tile(int i) const;
    int indexAt(juce::Point<int> p) const;
    std::vector<juce::Colour> colours_;
    juce::Colour selected_;
    int hover_ = -1;
};

// a text label on the left, a slider on the right
class LabelledSlider : public juce::Component {
public:
    LabelledSlider(const juce::String& text, double lo, double hi, double step);
    void resized() override;
    juce::Label label;
    juce::Slider slider;
};

class ParamSlider : public LabelledSlider {
public:
    ParamSlider(juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& text);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att_;
};

class ParamToggle : public juce::ToggleButton {
public:
    ParamToggle(juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& text);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att_;
};

class ParamCombo : public juce::Component {
public:
    ParamCombo(juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& text);
    void resized() override;

private:
    juce::Label label_;
    juce::ComboBox box_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att_;
};

// ------------------------------------------------------------------------------------------------
class SavePanel : public juce::Component {
public:
    explicit SavePanel(VerdigrisProcessor& p);
    std::function<void()> onDone;
    void open();
    void close();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void save();
    void updateNote();

    VerdigrisProcessor& proc_;
    juce::Label title_, note_;
    juce::TextEditor name_;
    juce::TextButton save_{"Save"}, cancel_{"Cancel"};
    juce::Rectangle<int> card_;
};

// ------------------------------------------------------------------------------------------------
class SettingsPanel : public juce::Component, private Settings::Listener, private juce::Timer {
public:
    static constexpr int kWidth = 372;

    explicit SettingsPanel(VerdigrisProcessor& p);
    ~SettingsPanel() override;

    std::function<void()> onClose;
    std::function<void(const juce::File&)> onVideoChosen;
    std::function<void()> onVideoRemoved;
    void setVideoStatus(const juce::String& text, bool isError);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void lookChanged() override { refresh(); }
    void timerCallback() override;

    void refresh();                      // bring every control in line with the saved look
    void showTab(int tab);
    void edit(const std::function<void(LookSettings&)>& fn); // change the look and save it
    void queueEdit(std::function<void(LookSettings&)> fn); // applied ~80 ms later (and only the last one)
    void flushEdit();
    void queueSliders();
    void openCustomColour(bool forBody, juce::Rectangle<int> tileInRow, juce::Component& row);
    void chooseVideo();
    void choosePhoto();

    VerdigrisProcessor& proc_;
    juce::SharedResourcePointer<Settings> settings_;
    bool refreshing_ = false;
    std::function<void(LookSettings&)> pending_; // an edit waiting for the throttle (sliders, custom colours)

    // header
    juce::TextButton tabLook_{"LOOK"}, tabSound_{"SOUND"}, done_{"Done"};
    juce::Viewport viewport_;
    int tab_ = 0;

    // ---- LOOK page ----
    FlowPage lookPage_;
    SwatchRow bodySwatches_;
    SwatchRow lightSwatches_;
    juce::TextButton texButtons_[5];
    LabelledSlider strength_{"Strength", 0.0, 1.2, 0.01};
    juce::TextButton photoChoose_{"Choose a picture..."}, photoRemove_{"Remove"};
    juce::Label photoName_;
    LabelledSlider photoOpacity_{"Picture amount", 0.0, 1.0, 0.01};
    juce::TextButton videoChoose_{"Load a video..."}, videoRemove_{"Remove"};
    juce::Label videoStatus_, videoHelp_;
    juce::TextButton sizeButtons_[3];
    juce::TextButton resetLook_{"Reset colours and texture"};
    std::unique_ptr<juce::FileChooser> chooser_;

    // ---- SOUND page ----
    FlowPage soundPage_;
    ParamSlider sag_, chopDepth_, chopSmooth_, washDecay_, tone_, mids_, lowcut_, gate_, mix_, supply_, hfe_, temp_, drift_, guitar_, pickup_,
        spread_, rampRate_, rampDepth_, inGain_, outGain_;
    ParamToggle rampFuzz_, rampBias_, rampTone_, rampSupply_, rampMix_;
    ParamCombo chopStep_, rampShape_, rampSync_;
    ParamSlider manualBpm_;
    juce::Label tempoNote_, tempoStatus_;
    juce::String shownTempo_;
    void updateTempo();
    juce::Label soundNote_;
    // MIDI learn
    juce::ComboBox midiTarget_;
    juce::StringArray midiIds_;
    juce::TextButton midiLearn_{"Learn"}, midiForgetAll_{"Forget all MIDI mappings"};
    juce::Label midiHelp_, midiList_;
    struct Poll : juce::Timer {
        std::function<void()> fn;
        void timerCallback() override { if (fn) fn(); }
    } midiPoll_;
    juce::String shownMidiSummary_;
    void updateMidi();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};
