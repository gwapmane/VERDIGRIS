// VERDIGRIS - tempo helpers (everything is counted in quarter-note beats). Plain C++, no JUCE.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace verdigris {

enum class RampShape : int { Sine = 0, Triangle = 1, Saw = 2, Random = 3 };

// ---- tempo tables (everything is counted in quarter-note beats) ------------------------------------------------------
constexpr int kNumStepRates = 8;
inline const char* stepRateName(int i) {
    static const char* n[kNumStepRates] = {"1/16", "1/8 triplet", "1/8", "1/4 triplet", "1/4", "1/2", "1 bar", "2 bars"};
    return n[std::min(kNumStepRates - 1, std::max(0, i))];
}
inline double stepRateBeats(int i, double bpb) {
    static const double b[kNumStepRates] = {0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0, 0.0, 0.0};
    i = std::min(kNumStepRates - 1, std::max(0, i));
    return i == 6 ? bpb : (i == 7 ? 2.0 * bpb : b[i]);
}
constexpr int kNumRampSyncs = 9;
inline const char* rampSyncName(int i) {
    static const char* n[kNumRampSyncs] = {"Off", "8 bars", "4 bars", "2 bars", "1 bar", "2 beats", "1 beat", "1/2 beat", "1/4 beat"};
    return n[std::min(kNumRampSyncs - 1, std::max(0, i))];
}
inline double rampSyncBeats(int i, double bpb) {
    const double b[kNumRampSyncs] = {0.0, 8 * bpb, 4 * bpb, 2 * bpb, bpb, 2.0, 1.0, 0.5, 0.25};
    return b[std::min(kNumRampSyncs - 1, std::max(0, i))];
}
// rhythm patterns: step values 0..1 (1 = open). The length of one step comes from "Step".
struct PatternDef {
    const char* name;
    int len;
    float v[16];
    float randomChance; // > 0: the steps are picked at random with this chance of being open
};
constexpr int kNumPatterns = 16;
inline const PatternDef& patternDef(int i) {
    static const PatternDef t[kNumPatterns] = {
        {"Off", 1, {1}, 0},
        {"Chop", 2, {1, 0}, 0},
        {"Off-beat", 2, {0, 1}, 0},
        {"Gallop", 4, {1, 0, 1, 1}, 0},
        {"Heartbeat", 8, {1, 0, 0.65f, 0, 0, 0, 0, 0}, 0},
        {"Bar start", 16, {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0}, 0},
        {"Late entry", 16, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1}, 0},
        {"Build", 16, {0.1f, 0.14f, 0.18f, 0.24f, 0.3f, 0.37f, 0.45f, 0.53f, 0.6f, 0.68f, 0.76f, 0.83f, 0.9f, 0.95f, 1, 1}, 0},
        {"Fade out", 16, {1, 1, 0.95f, 0.9f, 0.83f, 0.76f, 0.68f, 0.6f, 0.53f, 0.45f, 0.37f, 0.3f, 0.24f, 0.18f, 0.14f, 0.1f}, 0},
        {"Syncopated", 8, {1, 0, 0, 1, 0, 0, 1, 0}, 0},
        {"Clave", 16, {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0}, 0},
        {"Breath", 16, {0, 0.2f, 0.5f, 0.8f, 1, 1, 0.8f, 0.5f, 0.2f, 0, 0, 0, 0, 0, 0, 0}, 0},
        {"Stutter", 8, {1, 0, 1, 0, 1, 1, 0, 1}, 0},
        {"Pulse train", 4, {1, 0.35f, 0.7f, 0.35f}, 0},
        {"Scatter", 16, {1}, 0.5f},
        {"Dust", 16, {1}, 0.22f}};
    return t[std::min(kNumPatterns - 1, std::max(0, i))];
}

// where the host is in the song (the plugin fills this in for every block)
struct Transport {
    bool hostValid = false; // the host supplied a position
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0; // quarter notes, at the start of the block
    int beatsPerBar = 4;
};

} // namespace verdigris
