// Parameter IDs. These are what the DAW stores in projects and automation lanes, so once a
// build has shipped NEVER rename or renumber them - add new ones instead.
#pragma once

namespace ids {
// the pedal's own knobs and toggles
inline constexpr const char* fuzz = "fuzz";
inline constexpr const char* bias = "bias";
inline constexpr const char* volume = "volume";
inline constexpr const char* type = "type";       // Ge / Si
inline constexpr const char* voicing = "voicing"; // Fat / Zonk / Vin
// footswitch
inline constexpr const char* power = "power";
// the six side switches (textures) ...
inline constexpr const char* octave = "octave";
inline constexpr const char* starve = "starve";
inline constexpr const char* chop = "chop";
inline constexpr const char* crush = "crush";
inline constexpr const char* wash = "wash";
inline constexpr const char* swell = "swell";
// ... and the small knob under each
inline constexpr const char* octAmt = "octAmt";
inline constexpr const char* starveAmt = "starveAmt";
inline constexpr const char* chopPattern = "chopPattern";
inline constexpr const char* crushAmt = "crushAmt";
inline constexpr const char* washMix = "washMix";
inline constexpr const char* swellTime = "swellTime";
// more for the side switches (Settings > SOUND)
inline constexpr const char* sag = "sag";
inline constexpr const char* chopStep = "chopStep";
inline constexpr const char* chopDepth = "chopDepth";
inline constexpr const char* chopSmooth = "chopSmooth";
inline constexpr const char* washDecay = "washDecay";
// under the hood (Settings > SOUND)
inline constexpr const char* tone = "tone";
inline constexpr const char* gate = "gate";
inline constexpr const char* mix = "mix";
inline constexpr const char* hfe = "hfe";
inline constexpr const char* temp = "temp";
inline constexpr const char* drift = "drift";
inline constexpr const char* spread = "spread";
inline constexpr const char* pickup = "pickup";
inline constexpr const char* guitar = "guitar";
inline constexpr const char* lowcut = "lowcut";
inline constexpr const char* mids = "mids";
inline constexpr const char* supply = "supply";
// ramp
inline constexpr const char* rampRate = "rampRate";
inline constexpr const char* rampDepth = "rampDepth";
inline constexpr const char* rampShape = "rampShape";
inline constexpr const char* rampSync = "rampSync";
inline constexpr const char* rampFuzz = "rampFuzz";
inline constexpr const char* rampBias = "rampBias";
inline constexpr const char* rampTone = "rampTone";
inline constexpr const char* rampSupply = "rampSupply";
inline constexpr const char* rampMix = "rampMix";
inline constexpr const char* manualBpm = "manualBpm";
// trims
inline constexpr const char* inputGain = "inputGain";
inline constexpr const char* outputGain = "outputGain";
} // namespace ids
