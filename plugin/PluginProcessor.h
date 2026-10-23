#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

#include "../core/fuzz.h"
#include "ParamIds.h"
#include "PresetManager.h"

class VerdigrisProcessor : public juce::AudioProcessor {
public:
    VerdigrisProcessor();
    ~VerdigrisProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VERDIGRIS"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets{apvts}; // factory + user presets and the A / LIVE / B slots (state lives in apvts.state)

    // What the window and the pedal's lights listen to. Written by the audio thread, read by the editor ~30 times a second.
    //   visIn / visOut: loudest block peak since the last read (linear, 0..1+)
    //   uiClip: how hard the transistors are driven into the rails (0..1)
    //   uiSupply: the battery / supply voltage as the circuit sees it (sags when starved)
    //   uiSwell: the swell envelope gain; uiChop: the chop gate (1 open, 0 closed); uiChopStep: the step it is on
    //   uiRamp: what the ramp is doing (-1..1); uiBeat: song position in beats
    std::atomic<float> visIn{0.0f}, visOut{0.0f}, uiClip{0.0f}, uiSupply{9.0f}, uiSwell{1.0f}, uiChop{1.0f}, uiRamp{0.0f};
    std::atomic<int> uiChopStep{0};
    std::atomic<bool> uiGateOpen{true};
    std::atomic<double> uiBeat{0.0};

    // The oscilloscope: the signal before and after the circuit, squashed into "columns" (min / max of a few samples each).
    // The audio thread writes the newest column at scopeHead, the editor reads backwards from there.
    static constexpr int kScopeCols = 1024;
    std::array<std::atomic<float>, kScopeCols> scopeInMin{}, scopeInMax{}, scopeOutMin{}, scopeOutMax{};
    std::atomic<unsigned> scopeHead{0};
    std::atomic<int> scopeColSamples{9}, scopeLatencyCols{3};

    // ---- MIDI control: any MIDI CC can drive any parameter ("MIDI learn"). Saved with the project. ----
    void startMidiLearn(juce::RangedAudioParameter* p) { learnTarget_.store(p); } // the next CC that arrives is assigned to p
    void cancelMidiLearn() { learnTarget_.store(nullptr); }
    juce::RangedAudioParameter* midiLearnTarget() const { return learnTarget_.load(); }
    int midiCcOf(const juce::RangedAudioParameter* p) const; // -1 when none
    void clearMidiMapping(const juce::RangedAudioParameter* p);
    void clearAllMidiMappings();
    juce::String midiMappingSummary() const;
    // set when a CC has just been learned (the editor shows a short message)
    std::atomic<int> learnedCounter{0}, learnedCc{-1};
    std::atomic<juce::RangedAudioParameter*> learnedParam{nullptr};

    // ---- tempo: what the audio thread is following (the Sound tab shows it) ----
    std::atomic<float> tempoBpm{120.0f};
    std::atomic<bool> tempoFromHost{false};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    verdigris::Params readParams() const;
    verdigris::Transport readTransport(); // audio thread: tempo and song position from the DAW (or the Tempo slider)
    void handleCc(int cc, int value);
    static float ccToNormalised(const juce::RangedAudioParameter& p, int value);
    void saveMidiMap();
    void loadMidiMap();
    void feedScope(const float* in, const float* out, int n);

    std::array<std::atomic<juce::RangedAudioParameter*>, 128> ccMap_{};
    std::atomic<juce::RangedAudioParameter*> learnTarget_{nullptr};

    verdigris::Engine engine_;
    double sampleRate_ = 44100.0;
    std::vector<float> inL_, inR_, outL_, outR_;
    // scope column under construction
    int colCount_ = 0;
    float colInMin_ = 0, colInMax_ = 0, colOutMin_ = 0, colOutMax_ = 0;

    struct Raw {
        std::atomic<float> *fuzz, *bias, *volume, *type, *voicing, *power, *octave, *starve, *chop, *crush, *wash, *swell, *octAmt,
            *starveAmt, *chopPattern, *crushAmt, *washMix, *swellTime, *sag, *chopStep, *chopDepth, *chopSmooth, *washDecay, *tone, *gate,
            *mix, *hfe, *temp, *drift, *spread, *pickup, *guitar, *lowcut, *mids, *supply, *rampRate, *rampDepth, *rampShape, *rampSync,
            *rampFuzz, *rampBias, *rampTone, *rampSupply, *rampMix, *manualBpm, *inputGain, *outputGain;
    } raw_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerdigrisProcessor)
};
