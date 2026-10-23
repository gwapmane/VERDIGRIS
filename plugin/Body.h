// VERDIGRIS - the pedal's powder-coated body in the user's colour, optionally with a texture and/or
// the user's own picture. The artwork carries the lighting (bevel, grain, highlights) in three renders of the
// bare enclosure over black, grey and white. For any colour, each pixel is interpolated between the three, so
// the lighting always sits on top of whatever colour (or picture) is underneath.
#pragma once
#include "Settings.h"

namespace body {

// Returns the enclosure, 1120 x 1920 ARGB (opaque). Slow-ish (tens of ms): call on a colour/texture change only.
juce::Image build(const LookSettings& look, const juce::File& photoFile);

// Seat colours (the dark rings the knobs sit in): a mix of the body colour and a neutral.
juce::Colour seatA(juce::Colour body);
juce::Colour seatB(juce::Colour body);
juce::Colour seatC(juce::Colour body);

} // namespace body
