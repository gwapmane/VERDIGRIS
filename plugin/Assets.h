// VERDIGRIS - the pedal artwork. The hardware (knobs, toggles, footswitches) is rendered once, offline, into one
// sprite sheet (atlas.png) plus three renders of the bare enclosure (body_*.jpg, used to re-colour it). They are
// compiled into the plugin (BinaryData), so there are no extra files to install. Everything printed on the pedal is
// drawn in code, and the four body textures are made in code too.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "AtlasIndex.h"

namespace assets {

struct Raw {
    juce::Image atlas;              // all hardware sprites, 2x pixel density
    juce::Image body[3];            // the bare enclosure over black, grey and white (1120 x 1920)
    juce::Typeface::Ptr jost300, jost400, jost500, jost600;
};

const Raw& raw(); // loaded on first use

// Body texture 0..3 (brushed, hammered, slate, marble): a grey map, 1120 x 1920, made in code on first use.
const juce::Image& texture(int id);

// Jost at the given weight (300, 400, 500 or 600) and pixel height.
juce::Font font(int weight, float height);

// Area-averaging resize of a region of an ARGB image (good for shrinking; also copes with enlarging).
juce::Image resample(const juce::Image& src, juce::Rectangle<int> region, int destW, int destH);

// Re-colours white-ish printed text to a dark ink (same shape, same relative strength) for light-coloured bodies.
void inkify(juce::Image& img, juce::Colour ink);

// Every sprite of the atlas, resized for one display density. pxPerDesign = physical pixels per design pixel.
class Sprites {
public:
    void build(double pxPerDesign);
    double scale() const { return scale_; }
    const juce::Image& get(int id) const { return imgs_[static_cast<size_t>(id)]; }
    static const atlas::Sprite& meta(int id) { return atlas::table[id]; }
    void inkify(int firstId, int lastId, juce::Colour ink); // turn the text sprites dark (light bodies)

private:
    double scale_ = 0.0;
    std::vector<juce::Image> imgs_;
};

} // namespace assets
