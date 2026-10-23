// VERDIGRIS - presets. Factory bank (core/presets_factory.h) + the user's own presets (XML files in
// %APPDATA%\VERDIGRIS\Presets) + the A / LIVE / B memory slots of the pedal's preset switch.
//
// A preset recalls the SOUND only: the five pedal controls, the six side switches and their small knobs, the
// Settings > SOUND options, and the ramp. It never touches the footswitch (power), the trims or the Tempo slider.
//
// Everything the plugin must remember with a song (current preset name, the reference for the "modified"
// mark, the A/B slots and the switch position) is kept as properties on the parameter tree, so it is saved
// and restored by the host with the rest of the state.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

class PresetManager {
public:
    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts);

    struct Entry {
        juce::String category, name, hint;
        bool user = false;
        int factoryIndex = -1;
        juce::File file;
    };

    // ---- the list ----
    void rescanUser();
    const std::vector<Entry>& entries() const { return entries_; }
    int indexOfCurrent() const; // -1 when the current sound is not a listed preset

    // ---- using presets ----
    void apply(const Entry& e);
    void step(int direction); // next / previous in the list
    bool saveUser(const juce::String& name);
    bool deleteUser(const Entry& e);
    juce::String suggestName() const; // "My Preset 1", "My Preset 2", ...

    juce::String currentName() const;
    juce::String currentHint() const;
    bool currentIsUser() const;
    bool isModified() const; // knobs differ from the preset as recalled

    // ---- A / LIVE / B switch ----
    int slot() const; // 0 = A, 1 = LIVE, 2 = B
    void setSlot(int newSlot);
    bool slotHasMemory(int s) const;

    // the parameters a preset covers
    static const std::vector<const char*>& soundIds();

    static juce::File userFolder();

private:
    using Snapshot = std::vector<float>;
    Snapshot capture() const;
    void applySnapshot(const Snapshot& s);
    void setNorm(const char* id, float normalised);
    float getNorm(const char* id) const;
    void applyFactory(int index);
    bool applyFile(const juce::File& f);

    static juce::String toText(const Snapshot& s);
    static Snapshot fromText(const juce::String& t);

    void setCurrent(const juce::String& name, bool user, const juce::String& hint);

    juce::AudioProcessorValueTreeState& apvts_;
    std::vector<Entry> entries_;
};
