#include "PedalView.h"

#include <cmath>
#include <functional>

#include "Body.h"
#include "ParamIds.h"

namespace {

struct Pt { float x, y; };

// ---- where everything sits on the pedal (design pixels, pedal-local: the pedal is 560 x 960) ----
constexpr float kPW = 560.0f, kPH = 960.0f;
constexpr float kWinX = 172.0f, kWinY = 100.0f; // the scope window (216 x 396)

// rotary controls: 0 FUZZ, 1 BIAS, 2 VOLUME (big), 3..8 the small knobs under the side levers
struct Rot { float x, y, hit; const char* id; const char* name; int steps; };
const Rot kRot[9] = {
    {84, 606, 50, ids::fuzz, "FUZZ", 0},
    {476, 606, 50, ids::bias, "BIAS", 0},
    {280, 640, 66, ids::volume, "VOLUME", 0},
    {84, 212, 22, ids::octAmt, "BLEND", 0},
    {84, 344, 22, ids::starveAmt, "DEPTH", 0},
    {84, 476, 22, ids::chopPattern, "PATTERN", 16},
    {476, 212, 22, ids::crushAmt, "BITE", 0},
    {476, 344, 22, ids::washMix, "MIX", 0},
    {476, 476, 22, ids::swellTime, "TIME", 0},
};
// the six side levers: up = on, down = off
struct Side { float x, y; const char* id; const char* name; };
const Side kSide[6] = {
    {84, 144, ids::octave, "OCTAVE"}, {84, 276, ids::starve, "STARVE"}, {84, 408, ids::chop, "CHOP"},
    {476, 144, ids::crush, "CRUSH"},  {476, 276, ids::wash, "WASH"},    {476, 408, ids::swell, "SWELL"},
};
// the two selector levers
struct Sel { float x, y; const char* id; int count; const char* title; const char* text[3]; bool legendsRight; };
const Sel kSel[2] = {
    {84, 760, ids::type, 2, "TYPE", {"GE", "", "SI"}, true},
    {476, 760, ids::voicing, 3, "VOICING", {"FAT", "ZONK", "VIN"}, false},
};
constexpr Pt kPreset = {100, 872};
constexpr Pt kSlotLed = {100, 826};
constexpr Pt kPowerLed = {460, 872};
constexpr Pt kFoot = {280, 858};
constexpr double kHoldMs = 350.0;

const double kPi = 3.14159265358979323846;
const juce::Colour kRefAccent(0xffE8405E); // the colour the original light sprites were drawn for

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

juce::ColourGradient radial(float cx, float cy, float radius) {
    juce::ColourGradient gr(juce::Colours::transparentBlack, cx, cy, juce::Colours::transparentBlack, cx + radius, cy, true);
    gr.clearColours();
    return gr;
}

// a polished-metal knob top: a conic sheen like a turned aluminium cap, a knurled rim, a soft gloss. Still image: it
// does not need to turn, since a round cap looks the same at every angle (only the marker moves).
void paintMetalKnob(juce::Graphics& g, float cx, float cy, float r, juce::Colour seatA, juce::Colour seatB, juce::Colour seatC, int knurls) {
    // the cast shadow
    {
        juce::Path p;
        p.addEllipse(cx - r - 3.0f, cy - r - 3.0f, 2 * r + 6.0f, 2 * r + 6.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(0.62f), juce::roundToInt(r * 0.45f), {juce::roundToInt(r * 0.10f), juce::roundToInt(r * 0.20f)}).drawForPath(g, p);
    }
    // the seat it sits in
    {
        auto gr = radial(cx, cy, r * 1.32f);
        gr.addColour(0.0, seatA);
        gr.addColour(0.62, seatB);
        gr.addColour(1.0, seatC);
        g.setGradientFill(gr);
        g.fillEllipse(cx - r * 1.28f, cy - r * 1.28f, r * 2.56f, r * 2.56f);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawEllipse(cx - r * 1.28f, cy - r * 1.28f, r * 2.56f, r * 2.56f, 1.2f);
    }
    // the knurled rim
    {
        g.setColour(juce::Colour(0xff15171a));
        g.fillEllipse(cx - r, cy - r, 2 * r, 2 * r);
        for (int i = 0; i < knurls; ++i) {
            const float a = static_cast<float>(i) * 2.0f * static_cast<float>(kPi) / static_cast<float>(knurls);
            const float b = 0.5f + 0.5f * std::cos(a - 2.3f);
            g.setColour(juce::Colour::fromFloatRGBA(0.30f + 0.5f * b, 0.31f + 0.5f * b, 0.33f + 0.5f * b, 1.0f));
            g.drawLine(cx + (r - 3.4f) * std::sin(a), cy - (r - 3.4f) * std::cos(a), cx + (r - 0.4f) * std::sin(a), cy - (r - 0.4f) * std::cos(a),
                       juce::jmax(0.8f, r * 0.028f));
        }
    }
    // the face: conic highlights
    const float fr = r * 0.88f;
    const int wedges = 180;
    for (int i = 0; i < wedges; ++i) {
        const float a0 = static_cast<float>(i) * 2.0f * static_cast<float>(kPi) / wedges;
        const float a1 = a0 + 2.0f * static_cast<float>(kPi) / wedges * 1.15f;
        const float am = 0.5f * (a0 + a1);
        const float b = 0.46f + 0.26f * std::cos(am - 2.3f) + 0.13f * std::cos(2.0f * (am - 0.5f)) + 0.05f * std::cos(8.0f * am);
        juce::Path w;
        w.startNewSubPath(cx, cy);
        w.lineTo(cx + fr * std::sin(a0), cy - fr * std::cos(a0));
        w.lineTo(cx + fr * std::sin(a1), cy - fr * std::cos(a1));
        w.closeSubPath();
        g.setColour(juce::Colour::fromFloatRGBA(juce::jlimit(0.0f, 1.0f, b * 0.95f), juce::jlimit(0.0f, 1.0f, b * 0.97f), juce::jlimit(0.0f, 1.0f, b), 1.0f));
        g.fillPath(w);
    }
    // a shallow dish in the middle and a bright edge
    {
        auto gr = radial(cx - r * 0.12f, cy - r * 0.16f, r * 0.62f);
        gr.addColour(0.0, juce::Colours::black.withAlpha(0.0f));
        gr.addColour(0.82, juce::Colours::black.withAlpha(0.10f));
        gr.addColour(1.0, juce::Colours::black.withAlpha(0.22f));
        g.setGradientFill(gr);
        g.fillEllipse(cx - fr * 0.7f, cy - fr * 0.7f, fr * 1.4f, fr * 1.4f);
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawEllipse(cx - fr, cy - fr, 2 * fr, 2 * fr, juce::jmax(0.8f, r * 0.025f));
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawEllipse(cx - fr - 1.0f, cy - fr - 1.0f, 2 * fr + 2.0f, 2 * fr + 2.0f, 1.0f);
    }
    // the gloss
    {
        auto gr = radial(cx - r * 0.3f, cy - r * 0.42f, r * 0.8f);
        gr.addColour(0.0, juce::Colours::white.withAlpha(0.30f));
        gr.addColour(1.0, juce::Colours::white.withAlpha(0.0f));
        g.setGradientFill(gr);
        g.fillEllipse(cx - r * 0.95f, cy - r * 1.0f, r * 1.3f, r * 1.0f);
    }
}

} // namespace

// =================================================================================================== set-up
PedalView::PedalView(VerdigrisProcessor& p) : proc_(p) {
    scope_ = std::make_unique<ScopeView>(p);
    addAndMakeVisible(*scope_);
    // the wordmark, as one outline so it can be drawn several times for the fuzzy edge
    {
        const auto font = assets::font(600, 42.0f);
        juce::GlyphArrangement ga;
        float x = 0.0f;
        for (auto c : juce::String("VERDIGRIS")) {
            const auto s = juce::String::charToString(c);
            ga.addLineOfText(font, s, x, 0.0f);
            x += font.getStringWidthFloat(s) + 12.0f;
        }
        ga.createPath(wordPath_);
        const auto b = wordPath_.getBounds();
        wordPath_.applyTransform(juce::AffineTransform::translation(280.0f - b.getCentreX(), 50.0f - b.getCentreY()));
    }
    setBufferedToImage(true);
    setOpaque(true);
    startTimerHz(30);
    setScale(scale_);
}

void PedalView::setScale(float s) {
    scale_ = s;
    themeDirty_ = true;
    setSize(juce::roundToInt(kDesignW * s), juce::roundToInt(kDesignH * s));
    repaint();
}

void PedalView::resized() {
    const float s = scale_;
    scope_->setBounds(juce::Rectangle<float>((kPedalX + kWinX) * s, (kPedalY + kWinY) * s, ScopeView::kW * s, ScopeView::kH * s).toNearestInt());
    themeDirty_ = true;
}

void PedalView::setLook(const LookSettings& look, const juce::File& photoFile) {
    const bool bodyChanged = look.body != look_.body || look.texture != look_.texture || look.textureStrength != look_.textureStrength ||
                             look.photoFile != look_.photoFile || look.photoOpacity != look_.photoOpacity;
    look_ = look;
    photoFile_ = photoFile;
    if (bodyChanged) bodyDirty_ = true;
    themeDirty_ = true;
    scope_->setLook(look_.accent, body::seatA(look_.body));
    repaint();
}

juce::Colour PedalView::tint(juce::Colour c) const {
    const float dh = look_.accent.getHue() - kRefAccent.getHue();
    float sScale = kRefAccent.getSaturation() > 0.0f ? look_.accent.getSaturation() / kRefAccent.getSaturation() : 1.0f;
    sScale = juce::jlimit(0.0f, 1.4f, sScale);
    float h = c.getHue() + dh;
    h -= std::floor(h);
    return juce::Colour::fromHSV(h, clamp01(c.getSaturation() * sScale), c.getBrightness(), c.getFloatAlpha());
}

// =================================================================================================== state
float PedalView::rotValue(int i) const {
    if (auto* p = param(kRot[i].id)) return p->getValue();
    return 0.0f;
}

juce::String PedalView::rotText(int i) const {
    const float v = rotValue(i);
    if (i == 5) return juce::String(verdigris::patternDef(juce::roundToInt(v * 15.0f)).name).toUpperCase();
    if (i == 2) return juce::String(v * 10.0f, 1);
    return juce::String(juce::roundToInt(v * 100.0f)) + "%";
}

bool PedalView::sideOn(int s) const { return boolParam(kSide[s].id); }

// how hard each side effect is working, for its glow
float PedalView::sideActivity(int s) const {
    switch (s) {
        case 0: return clamp01(scope_->level() * 1.2f);                                           // octave: follows the level
        case 1: return clamp01((9.0f - proc_.uiSupply.load(std::memory_order_relaxed)) / 4.0f);  // starve: how far the battery has sagged
        case 2: return clamp01(proc_.uiChop.load(std::memory_order_relaxed));                    // chop: the gate opening and closing
        case 3: return clamp01(scope_->clipping() * 1.3f);                                        // crush: how hard it is clipping
        case 4: return clamp01(scope_->level());                                                  // wash: follows the level
        default: return clamp01(proc_.uiSwell.load(std::memory_order_relaxed));                  // swell: the swell envelope
    }
}

int PedalView::selectIndex(int t) const {
    if (t == 2) return proc_.presets.slot();
    return juce::jlimit(0, kSel[t].count - 1, juce::roundToInt(proc_.apvts.getRawParameterValue(kSel[t].id)->load()));
}

int PedalView::selectPos(int t) const {
    const int idx = selectIndex(t);
    if (t == 0) return idx == 0 ? 0 : 2; // TYPE has two positions: up and down
    return idx;
}

void PedalView::setRot(int i, float v) {
    auto* p = param(kRot[i].id);
    if (p == nullptr) return;
    v = clamp01(v);
    if (kRot[i].steps > 1) v = std::round(v * (kRot[i].steps - 1)) / static_cast<float>(kRot[i].steps - 1);
    p->setValueNotifyingHost(v);
}

void PedalView::setSelectPos(int t, int pos) {
    pos = juce::jlimit(0, 2, pos);
    if (t == 2) {
        proc_.presets.setSlot(pos);
        return;
    }
    const int idx = t == 0 ? (pos >= 1 ? 1 : 0) : pos;
    if (auto* p = param(kSel[t].id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(static_cast<float>(idx) / static_cast<float>(kSel[t].count - 1));
        p->endChangeGesture();
    }
}

void PedalView::setSide(int s, bool on) {
    if (auto* p = param(kSide[s].id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(on ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void PedalView::setFoot(bool on) {
    if (auto* p = param(ids::power)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(on ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

// =================================================================================================== caches
void PedalView::ensureCaches(double phys) {
    const double u = scale_ * phys;
    if (bodyDirty_) {
        bodyFull_ = body::build(look_, photoFile_);
        bodyDirty_ = false;
        themeDirty_ = true;
        // is the body light? (average brightness of a sample of its pixels)
        double sum = 0.0;
        int n = 0;
        for (int y = 40; y < bodyFull_.getHeight(); y += 24)
            for (int x = 40; x < bodyFull_.getWidth(); x += 24) {
                const auto c = bodyFull_.getPixelAt(x, y);
                sum += 0.299 * c.getFloatRed() + 0.587 * c.getFloatGreen() + 0.114 * c.getFloatBlue();
                ++n;
            }
        light_ = n > 0 && sum / n > 0.42;
        ink_ = light_ ? juce::Colour(0xff1c1215) : juce::Colour(0xffe9e6e1);
    }
    if (std::abs(u - u_) > 1.0e-6) {
        u_ = u;
        sprites_.build(u);
        themeDirty_ = true;
    }
    if (themeDirty_) {
        rebuildThemeSprites(u);
        rebuildStatic(u);
        themeDirty_ = false;
    }
}

// the small pieces that carry the lights colour: dots and the indicator jewels
void PedalView::rebuildThemeSprites(double u) {
    auto make = [&](float w, float h, const std::function<void(juce::Graphics&)>& draw) {
        juce::Image im(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(w * u)), juce::jmax(1, juce::roundToInt(h * u)), true);
        juce::Graphics g(im);
        g.addTransform(juce::AffineTransform::scale(static_cast<float>(u)));
        draw(g);
        return im;
    };
    // a lit dot: a small bead of light with a soft glow
    dotLit_ = make(15, 15, [&](juce::Graphics& g) {
        const auto lit = look_.accent.interpolatedWith(juce::Colours::white, 0.7f);
        auto glow = radial(7.5f, 7.5f, 7.0f);
        glow.addColour(0.0, look_.accent.withAlpha(0.65f));
        glow.addColour(0.45, look_.accent.withAlpha(0.25f));
        glow.addColour(1.0, look_.accent.withAlpha(0.0f));
        g.setGradientFill(glow);
        g.fillEllipse(0.5f, 0.5f, 14, 14);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(5.0f, 5.5f, 5, 5);
        auto gr = radial(6.75f, 6.5f, 3.6f);
        gr.addColour(0.0, juce::Colours::white);
        gr.addColour(0.6, lit);
        gr.addColour(1.0, look_.accent);
        g.setGradientFill(gr);
        g.fillEllipse(5.0f, 5.0f, 5, 5);
    });
    // an unlit dot: a dark bead in the lights colour
    dotOff_ = make(15, 15, [&](juce::Graphics& g) {
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillEllipse(5.0f, 6.0f, 5, 5);
        auto gr = radial(6.75f, 6.5f, 3.6f);
        gr.addColour(0.0, tint(juce::Colour(0xffa83a5a)));
        gr.addColour(0.6, tint(juce::Colour(0xff7a1d38)));
        gr.addColour(1.0, tint(juce::Colour(0xff4a0f21)));
        g.setGradientFill(gr);
        g.fillEllipse(5.0f, 5.0f, 5, 5);
    });
    // the jewel lenses (16 px), with room around them for the glow
    auto lens = [&](bool on) {
        return make(36, 36, [&](juce::Graphics& g) {
            const float cx = 18, cy = 18;
            if (on) {
                auto glow = radial(cx, cy, 13.0f);
                const auto c = tint(juce::Colour(0xffe8405e));
                glow.addColour(0.0, c.withAlpha(0.60f));
                glow.addColour(0.62, c.withAlpha(0.45f));
                glow.addColour(1.0, c.withAlpha(0.0f));
                g.setGradientFill(glow);
                g.fillEllipse(cx - 13, cy - 13, 26, 26);
            }
            auto gr = radial(cx - 2.24f, cy - 3.2f, 15.2f);
            if (on) {
                gr.addColour(0.0, tint(juce::Colour(0xffffc2cf)));
                gr.addColour(0.36, tint(juce::Colour(0xffe8405e)));
                gr.addColour(0.82, tint(juce::Colour(0xff9a1535)));
                gr.addColour(1.0, tint(juce::Colour(0xff9a1535)));
            } else {
                gr.addColour(0.0, tint(juce::Colour(0xff7a3347)));
                gr.addColour(0.55, tint(juce::Colour(0xff4a1022)));
                gr.addColour(1.0, tint(juce::Colour(0xff2a0712)));
            }
            g.setGradientFill(gr);
            g.fillEllipse(cx - 8, cy - 8, 16, 16);
            g.setColour((on ? juce::Colour(0xff3c000f).withAlpha(0.5f) : juce::Colour(0xff140006).withAlpha(0.7f)));
            g.drawEllipse(cx - 7.5f, cy - 7.5f, 15, 15, 1.0f);
            if (!on) { // the dark inner lip of an unlit jewel
                juce::Path hole;
                hole.addEllipse(cx - 8, cy - 8, 16, 16);
                juce::Path frame;
                frame.setUsingNonZeroWinding(false);
                frame.addRectangle(hole.getBounds().expanded(12));
                frame.addPath(hole);
                g.saveState();
                g.reduceClipRegion(hole);
                juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 3, {0, 2}).drawForPath(g, frame);
                g.restoreState();
            }
        });
    };
    ledOn_ = lens(true);
    ledOff_ = lens(false);
}

void PedalView::rebuildStatic(double u) {
    const int W = juce::jmax(1, juce::roundToInt(getWidth() * (u / scale_)));
    const int H = juce::jmax(1, juce::roundToInt(getHeight() * (u / scale_)));
    bodyScaled_ = assets::resample(bodyFull_, bodyFull_.getBounds(), juce::roundToInt(kPW * u), juce::roundToInt(kPH * u));
    staticLayer_ = juce::Image(juce::Image::ARGB, W, H, true);
    juce::Graphics g(staticLayer_);
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(u)));
    paintStatic(g, u);
}

// =================================================================================================== drawing helpers
void PedalView::blitImage(juce::Graphics& g, const juce::Image& img, float x, float y) const {
    const float px = static_cast<float>(std::round(x * u_) / u_), py = static_cast<float>(std::round(y * u_) / u_);
    g.drawImage(img, juce::Rectangle<float>(px, py, static_cast<float>(img.getWidth() / u_), static_cast<float>(img.getHeight() / u_)),
                juce::RectanglePlacement::stretchToFit);
}

void PedalView::blit(juce::Graphics& g, int sprite, float ax, float ay) const {
    const auto& m = assets::Sprites::meta(sprite);
    blitImage(g, sprites_.get(sprite), ax + m.ox, ay + m.oy);
}

void PedalView::drawTracked(juce::Graphics& g, const juce::String& text, float cx, float cy, const juce::Font& font, float tracking) const {
    float w = -tracking;
    for (auto c : text) w += font.getStringWidthFloat(juce::String::charToString(c)) + tracking;
    float x = cx - w * 0.5f;
    g.setFont(font);
    for (auto c : text) {
        const auto s = juce::String::charToString(c);
        const float cw = font.getStringWidthFloat(s);
        g.drawText(s, juce::Rectangle<float>(x - 2.0f, cy - font.getHeight() * 0.5f, cw + 4.0f, font.getHeight()), juce::Justification::centred, false);
        x += cw + tracking;
    }
}


juce::Rectangle<float> PedalView::wordmarkArea() const {
    return {kPedalX + 20.0f, kPedalY + 4.0f, kPW - 40.0f, 84.0f};
}

// =================================================================================================== the still parts
void PedalView::paintStatic(juce::Graphics& g, double /*u*/) {
    // ---- the floor under the pedal ----
    {
        juce::ColourGradient floor(juce::Colour(0xff1b2024), 0, 0, juce::Colour(0xff0a0c0e), 0, kDesignH, false);
        floor.addColour(0.52, juce::Colour(0xff12161a));
        g.setGradientFill(floor);
        g.fillRect(0.0f, 0.0f, kDesignW, kDesignH);
        g.saveState();
        g.addTransform(juce::AffineTransform::scale(0.60f * kDesignW, 0.46f * kDesignH).translated(0.5f * kDesignW, 0.40f * kDesignH));
        auto gr = radial(0, 0, 1.0f);
        gr.addColour(0.0, juce::Colour(0xffe4e6e9).withAlpha(0.12f));
        gr.addColour(0.72, juce::Colour(0xffe4e6e9).withAlpha(0.0f));
        gr.addColour(1.0, juce::Colour(0xffe4e6e9).withAlpha(0.0f));
        g.setGradientFill(gr);
        g.fillEllipse(-1, -1, 2, 2);
        g.restoreState();
    }

    g.addTransform(juce::AffineTransform::translation(kPedalX, kPedalY));
    const juce::Colour pin = look_.accent.darker(0.35f).withAlpha(0.9f);

    // ---- side jacks, tucked behind the enclosure ----
    blit(g, atlas::jack_l, -34, 300);
    blit(g, atlas::jack_r, 554, 300);
    blit(g, atlas::jack_l, -34, 470);
    blit(g, atlas::jack_r, 554, 470);

    // ---- the pedal's shadow on the floor ----
    {
        auto shadow = [&](float inset, float corner, int dx, int dy, int blur, float alpha) {
            juce::Path p;
            p.addRoundedRectangle(inset, inset, kPW - 2 * inset, kPH - 2 * inset, juce::jmax(2.0f, corner - inset));
            juce::DropShadow(juce::Colours::black.withAlpha(alpha), blur, {dx, dy}).drawForPath(g, p);
        };
        shadow(18.0f, 30.0f, 34, 70, 48, 0.65f);
        shadow(2.0f, 30.0f, 10, 24, 17, 0.50f);
        shadow(0.0f, 30.0f, 2, 5, 4, 0.45f);
        shadow(0.0f, 30.0f, 0, 1, 1, 0.75f);
    }

    // ---- the body ----
    {
        juce::Path shape;
        shape.addRoundedRectangle(0.0f, 0.0f, kPW, kPH, 30.0f);
        g.saveState();
        g.reduceClipRegion(shape);
        blitImage(g, bodyScaled_, 0, 0);
        g.restoreState();
    }
    // ---- printed pinstripe ----
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(11.625f, 12.625f, 533.75f, 933.75f, 19.4f, 1.25f);
    g.setColour(pin);
    g.drawRoundedRectangle(11.625f, 11.625f, 533.75f, 933.75f, 19.4f, 1.25f);

    // ---- the window plate: a bevelled metal frame with the glass set into it ----
    {
        const juce::Rectangle<float> plate(156.0f, 84.0f, 248.0f, 428.0f);
        juce::Path pp;
        pp.addRoundedRectangle(plate, 26.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 14, {0, 5}).drawForPath(g, pp);
        juce::ColourGradient m(juce::Colour(0xffb3b9bf), plate.getX(), plate.getY(), juce::Colour(0xff373c42), plate.getRight(), plate.getBottom(), false);
        m.addColour(0.45, juce::Colour(0xff6c737a));
        g.setGradientFill(m);
        g.fillPath(pp);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawRoundedRectangle(plate.reduced(0.7f), 25.5f, 1.2f);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawRoundedRectangle(plate.reduced(1.9f), 24.5f, 1.0f);
        // the dark well around the glass
        const juce::Rectangle<float> well(166.0f, 94.0f, 228.0f, 408.0f);
        g.setColour(juce::Colour(0xff07080a));
        g.fillRoundedRectangle(well, 20.0f);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(well, 20.0f, 1.5f);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawRoundedRectangle(well.expanded(0.8f), 20.5f, 0.8f);
        // four screws
        const Pt screws[4] = {{167, 95}, {393, 95}, {167, 501}, {393, 501}};
        for (int i = 0; i < 4; ++i) {
            const float x = screws[i].x, y = screws[i].y;
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.fillEllipse(x - 4.0f, y - 3.2f, 8.0f, 8.0f);
            auto gr = radial(x - 1.0f, y - 1.2f, 4.6f);
            gr.addColour(0.0, juce::Colour(0xffe2e5e8));
            gr.addColour(0.7, juce::Colour(0xff8a9097));
            gr.addColour(1.0, juce::Colour(0xff4e545a));
            g.setGradientFill(gr);
            g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            g.setColour(juce::Colour(0xff20252a));
            const float a = 0.6f + 1.3f * static_cast<float>(i);
            g.drawLine(x - 2.6f * std::cos(a), y - 2.6f * std::sin(a), x + 2.6f * std::cos(a), y + 2.6f * std::sin(a), 1.0f);
        }
    }

    // ---- FUZZ and BIAS: the hardware knobs' seats, cast shadows and rings ----
    for (int i = 0; i < 2; ++i) {
        const float ax = kRot[i].x - 49, ay = kRot[i].y - 49;
        juce::Path disc;
        disc.addEllipse(ax, ay, 98, 98);
        auto gr = radial(kRot[i].x, kRot[i].y, 69.3f);
        gr.addColour(0.0, body::seatA(look_.body));
        gr.addColour(0.56, body::seatB(look_.body));
        gr.addColour(1.0, body::seatC(look_.body));
        g.setGradientFill(gr);
        g.fillPath(disc);
        blit(g, atlas::knob_seat_ov, ax, ay);
        blit(g, atlas::knob_shadow, ax, ay);
        blit(g, atlas::knob_ring, ax, ay);
    }
    // ---- VOLUME and the six small knobs: drawn in code ----
    paintMetalKnob(g, kRot[2].x, kRot[2].y, 62.0f, body::seatA(look_.body), body::seatB(look_.body), body::seatC(look_.body), 72);
    for (int i = 3; i < 9; ++i)
        paintMetalKnob(g, kRot[i].x, kRot[i].y, 16.5f, body::seatA(look_.body), body::seatB(look_.body), body::seatC(look_.body), 28);
    // the printed 0..10 scale around VOLUME
    {
        const float cx = kRot[2].x, cy = kRot[2].y;
        for (int i = 0; i <= 20; ++i) {
            const float a = static_cast<float>((-135.0 + i * 13.5) * kPi / 180.0);
            const float r0 = 71.0f, r1 = (i % 2 == 0) ? 84.0f : 78.0f;
            g.setColour(ink_.withAlpha(i % 2 == 0 ? 0.62f : 0.38f));
            g.drawLine(cx + r0 * std::sin(a), cy - r0 * std::cos(a), cx + r1 * std::sin(a), cy - r1 * std::cos(a), i % 2 == 0 ? 1.5f : 1.0f);
            if (i % 2 == 0) {
                g.setColour(ink_.withAlpha(0.78f));
                g.setFont(assets::font(500, 10.5f));
                const float tr = 97.0f;
                g.drawText(juce::String(i / 2), juce::Rectangle<float>(cx + tr * std::sin(a) - 12.0f, cy - tr * std::cos(a) - 7.0f, 24.0f, 14.0f),
                           juce::Justification::centred, false);
            }
        }
    }
    // ---- lever seats and nuts: six side levers, TYPE, VOICING and the A / LIVE / B lever ----
    std::vector<Pt> togs;
    for (const auto& s : kSide) togs.push_back({s.x, s.y});
    for (const auto& s : kSel) togs.push_back({s.x, s.y});
    togs.push_back(kPreset);
    for (const auto& t : togs) {
        const float ax = t.x - 32, ay = t.y - 32;
        juce::Path disc;
        disc.addEllipse(ax + 10, ay + 10, 44, 44);
        auto gr = radial(t.x, t.y, 31.1f);
        gr.addColour(0.0, body::seatA(look_.body));
        gr.addColour(0.70, body::seatB(look_.body));
        gr.addColour(1.0, body::seatC(look_.body));
        g.setGradientFill(gr);
        g.fillPath(disc);
        blit(g, atlas::tog_seat_ov, ax, ay);
        blit(g, atlas::tog_nut, ax, ay);
    }
    // ---- indicator housings and the footswitch base ----
    blit(g, atlas::led_housing, kSlotLed.x - 13, kSlotLed.y - 13);
    blit(g, atlas::led_housing, kPowerLed.x - 13, kPowerLed.y - 13);
    blit(g, atlas::foot_base, kFoot.x - 70, kFoot.y - 70);

    // ---- printed text that never changes ----
    g.setColour(ink_.withAlpha(0.78f));
    for (const auto& s : kSide) drawTracked(g, s.name, s.x, s.y + 37.0f, assets::font(500, 9.5f), 2.0f);
    for (const auto& s : kSel) drawTracked(g, s.title, s.x, s.y - 48.0f, assets::font(500, 10.5f), 2.2f);
    drawTracked(g, "FUZZ", kFoot.x, 938.0f, assets::font(500, 11.0f), 2.6f);
    drawTracked(g, "ON", kPowerLed.x, kPowerLed.y + 30.0f, assets::font(500, 11.0f), 2.6f);
    g.setColour(ink_.withAlpha(0.45f));
    drawTracked(g, "HOLD = MOMENTARY", kFoot.x, 951.0f, assets::font(400, 7.5f), 1.4f);
    g.setColour(ink_.withAlpha(0.78f));
    drawTracked(g, "A", kPreset.x - 50.0f, kPreset.y, assets::font(500, 11.0f), 1.0f);
    drawTracked(g, "B", kPreset.x + 50.0f, kPreset.y, assets::font(500, 11.0f), 1.0f);
    g.setColour(ink_.withAlpha(0.5f));
    drawTracked(g, "LIVE", kPreset.x, kPreset.y + 40.0f, assets::font(400, 8.5f), 2.0f);
    // two short rules either side of the wordmark's underline
    g.setColour(look_.accent.withAlpha(0.5f));
    g.fillRect(190.0f, 80.0f, 180.0f, 1.0f);
}

// =================================================================================================== the moving parts
void PedalView::paintWordmark(juce::Graphics& g) {
    const float lv = scope_->level();
    const float clip = scope_->clipping();
    const float live = clamp01(lv * 0.9f + clip * 0.5f);
    // the fuzzy edge: two ghosts pulled apart the harder the circuit clips
    const float off = 0.8f + 5.5f * clip;
    g.setColour(look_.accent.withAlpha(0.10f + 0.45f * clip));
    g.fillPath(wordPath_, juce::AffineTransform::translation(-off, 0.0f));
    g.setColour(look_.accent.interpolatedWith(juce::Colours::white, 0.5f).withAlpha(0.06f + 0.30f * clip));
    g.fillPath(wordPath_, juce::AffineTransform::translation(off, 0.0f));
    // a soft halo that opens with the level
    const float halo = 0.08f + 0.5f * live;
    g.setColour(look_.accent.withAlpha(halo * 0.5f));
    g.strokePath(wordPath_, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(look_.accent.withAlpha(halo * 0.7f));
    g.strokePath(wordPath_, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // the word itself
    g.setColour(ink_.interpolatedWith(look_.accent.interpolatedWith(juce::Colours::white, 0.6f), clamp01(live * 0.8f)));
    g.fillPath(wordPath_);
}

void PedalView::paintVolumeScale(juce::Graphics& g) const {
    const float cx = kRot[2].x, cy = kRot[2].y;
    const int lit = juce::roundToInt(rotValue(2) * 20.0f);
    for (int i = 0; i <= lit; ++i) {
        const float a = static_cast<float>((-135.0 + i * 13.5) * kPi / 180.0);
        const float r0 = 71.0f, r1 = (i % 2 == 0) ? 84.0f : 78.0f;
        g.setColour(look_.accent.withAlpha(0.22f));
        g.drawLine(cx + r0 * std::sin(a), cy - r0 * std::cos(a), cx + r1 * std::sin(a), cy - r1 * std::cos(a), 4.0f);
        g.setColour(look_.accent.interpolatedWith(juce::Colours::white, 0.35f));
        g.drawLine(cx + r0 * std::sin(a), cy - r0 * std::cos(a), cx + r1 * std::sin(a), cy - r1 * std::cos(a), i % 2 == 0 ? 2.0f : 1.4f);
    }
}

void PedalView::paintDynamic(juce::Graphics& g) {
    paintWordmark(g);

    // ---- the lights behind the side levers: they glow while the effect is on, and work with it ----
    for (int s = 0; s < 6; ++s) {
        if (!sideOn(s)) continue;
        const float act = sideActivity(s);
        auto gr = radial(kSide[s].x, kSide[s].y, 52.0f);
        gr.addColour(0.0, look_.accent.withAlpha(0.20f + 0.40f * act));
        gr.addColour(0.55, look_.accent.withAlpha(0.07f + 0.16f * act));
        gr.addColour(1.0, look_.accent.withAlpha(0.0f));
        g.setGradientFill(gr);
        g.fillEllipse(kSide[s].x - 52.0f, kSide[s].y - 52.0f, 104.0f, 104.0f);
    }

    // ---- dots around FUZZ and BIAS ----
    for (int k = 0; k < 2; ++k) {
        const int n = 11;
        const int sel = juce::roundToInt(rotValue(k) * (n - 1));
        for (int i = 0; i < n; ++i) {
            const double a = (-135.0 + i * 270.0 / (n - 1)) * kPi / 180.0;
            const float x = kRot[k].x + 56.0f * static_cast<float>(std::sin(a));
            const float y = kRot[k].y - 56.0f * static_cast<float>(std::cos(a));
            blitImage(g, i <= sel ? dotLit_ : dotOff_, x - 7.5f, y - 7.5f);
        }
    }
    // ---- the lit part of the VOLUME scale ----
    paintVolumeScale(g);
    // ---- dots around the small knobs ----
    for (int k = 3; k < 9; ++k) {
        const int steps = kRot[k].steps;
        const int n = steps > 1 ? steps : 11;
        const int sel = juce::roundToInt(rotValue(k) * (n - 1));
        for (int i = 0; i < n; ++i) {
            const double a = (-135.0 + i * 270.0 / (n - 1)) * kPi / 180.0;
            const float x = kRot[k].x + 26.0f * static_cast<float>(std::sin(a));
            const float y = kRot[k].y - 26.0f * static_cast<float>(std::cos(a));
            const bool on = steps > 1 ? (i == sel) : (i <= sel);
            const float d = steps > 1 ? 3.0f : 3.4f;
            if (on) {
                g.setColour(look_.accent.withAlpha(0.28f));
                g.fillEllipse(x - d, y - d, 2 * d, 2 * d);
                g.setColour(look_.accent.interpolatedWith(juce::Colours::white, 0.55f));
                g.fillEllipse(x - d * 0.5f, y - d * 0.5f, d, d);
            } else {
                g.setColour(look_.accent.darker(0.65f).withAlpha(0.75f));
                g.fillEllipse(x - d * 0.5f, y - d * 0.5f, d, d);
            }
        }
    }
    // ---- the sprite knobs (FUZZ, BIAS) ----
    for (int k = 0; k < 2; ++k) {
        const float cx = kRot[k].x, cy = kRot[k].y, ax = cx - 49, ay = cy - 49;
        const float ang = static_cast<float>((-135.0 + rotValue(k) * 270.0) * kPi / 180.0);
        {   // the knurled edge turns with the knob
            const auto& m = assets::Sprites::meta(atlas::knob_knurl);
            const auto& img = sprites_.get(atlas::knob_knurl);
            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(ang, cx, cy));
            g.drawImage(img, juce::Rectangle<float>(ax + m.ox, ay + m.oy, static_cast<float>(img.getWidth() / u_), static_cast<float>(img.getHeight() / u_)),
                        juce::RectanglePlacement::stretchToFit);
            g.restoreState();
        }
        blit(g, atlas::knob_cap, ax, ay);
        {   // the indicator line, in the lights colour
            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(ang, cx, cy));
            const juce::Rectangle<float> bar(ax + 47, ay + 19, 4, 21);
            g.setColour(juce::Colour(0xffedeff0).withAlpha(0.65f));
            g.fillRoundedRectangle(bar.translated(0, 1), 2.0f);
            juce::ColourGradient gr(tint(juce::Colour(0xff4f0f23)), bar.getX(), 0, tint(juce::Colour(0xff4a0f21)), bar.getRight(), 0, false);
            gr.addColour(0.5, tint(juce::Colour(0xff942548)));
            g.setGradientFill(gr);
            g.fillRoundedRectangle(bar, 2.0f);
            g.restoreState();
        }
        blit(g, atlas::knob_gloss, ax, ay);
    }
    // ---- the markers on VOLUME and the small knobs ----
    for (int k = 2; k < 9; ++k) {
        const float cx = kRot[k].x, cy = kRot[k].y;
        const float ang = static_cast<float>((-135.0 + rotValue(k) * 270.0) * kPi / 180.0);
        const bool big = k == 2;
        const juce::Rectangle<float> bar = big ? juce::Rectangle<float>(cx - 2.4f, cy - 50.0f, 4.8f, 28.0f) : juce::Rectangle<float>(cx - 1.5f, cy - 13.5f, 3.0f, 9.0f);
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(ang, cx, cy));
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(bar.translated(0.0f, 0.8f), bar.getWidth() * 0.5f);
        g.setColour(look_.accent.darker(0.15f));
        g.fillRoundedRectangle(bar, bar.getWidth() * 0.5f);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.fillRoundedRectangle(bar.reduced(bar.getWidth() * 0.34f, 0.8f).withHeight(bar.getHeight() * 0.8f), 0.8f);
        g.restoreState();
    }
    // ---- knob names - or the value while you are on one ----
    for (int k = 0; k < 9; ++k) {
        const bool showValue = (hoverRot_ == k) || (drag_.kind == Hit::Rotary && drag_.index == k);
        const float cx = kRot[k].x, cy = kRot[k].y;
        const float ty = k < 2 ? cy + 57.0f : (k == 2 ? cy + 84.0f : cy + 25.0f);
        const float sz = k < 3 ? 10.5f : 7.5f;
        if (showValue) {
            g.setColour(look_.accent.interpolatedWith(juce::Colours::white, light_ ? 0.0f : 0.55f));
            drawTracked(g, rotText(k), cx, ty, assets::font(600, k < 3 ? 10.5f : 9.5f), 1.4f);
        } else {
            g.setColour(ink_.withAlpha(k < 3 ? 0.8f : 0.55f));
            drawTracked(g, kRot[k].name, cx, ty, assets::font(k < 3 ? 500 : 400, sz), k < 3 ? 2.2f : 1.5f);
        }
    }
    // ---- legends beside the levers ----
    auto legend = [&](float cx, float cy, bool right, int row, const juce::String& text, bool on) {
        const float rowY = cy - 7 + (row - 1) * 14;
        const auto font = assets::font(on ? 600 : 400, 10.0f);
        const float w = font.getStringWidthFloat(text);
        const float textX = right ? cx + 39 : cx - 39 - w;
        g.setColour(on ? ink_ : ink_.withAlpha(0.5f));
        g.setFont(font);
        g.drawText(text, juce::Rectangle<float>(textX, rowY - 1.0f, w + 6.0f, 14.0f), juce::Justification::centredLeft, false);
        const float dotX = right ? cx + 30 : cx - 34;
        const float dotY = rowY + 5;
        if (on) {
            auto glow = radial(dotX + 2, dotY + 2, 5.0f);
            glow.addColour(0.0, look_.accent.withAlpha(0.65f));
            glow.addColour(1.0, look_.accent.withAlpha(0.0f));
            g.setGradientFill(glow);
            g.fillEllipse(dotX - 3, dotY - 3, 10, 10);
            g.setColour(look_.accent.interpolatedWith(juce::Colours::white, 0.55f));
        } else {
            g.setColour(look_.accent.darker(0.5f));
        }
        g.fillEllipse(dotX, dotY, 4, 4);
    };
    for (int s = 0; s < 6; ++s) {
        const bool on = sideOn(s), right = kSide[s].x < 280.0f;
        legend(kSide[s].x, kSide[s].y, right, 0, "ON", on);
        legend(kSide[s].x, kSide[s].y, right, 2, "OFF", !on);
    }
    for (int t = 0; t < 2; ++t) {
        const int pos = selectPos(t);
        for (int i = 0; i < 3; ++i) {
            if (kSel[t].count == 2 && i == 1) continue;
            legend(kSel[t].x, kSel[t].y, kSel[t].legendsRight, i, kSel[t].text[i], pos == i);
        }
    }
    // ---- the levers ----
    for (int s = 0; s < 6; ++s) blit(g, sideOn(s) ? atlas::tog_lv_up : atlas::tog_lv_down, kSide[s].x - 32, kSide[s].y - 32);
    for (int t = 0; t < 3; ++t) {
        const float x = t < 2 ? kSel[t].x : kPreset.x, y = t < 2 ? kSel[t].y : kPreset.y;
        const int pos = selectPos(t);
        int id;
        if (t < 2) id = pos == 0 ? atlas::tog_lv_up : (pos == 1 ? atlas::tog_lv_mid : atlas::tog_lv_down);
        else id = pos == 0 ? atlas::tog_lh_left : (pos == 1 ? atlas::tog_lv_mid : atlas::tog_lh_right);
        blit(g, id, x - 32, y - 32);
    }
    // ---- indicator jewels: a preset slot is in use, and On ----
    {
        const bool on[2] = {proc_.presets.slot() != 1, boolParam(ids::power)};
        const Pt at[2] = {kSlotLed, kPowerLed};
        for (int i = 0; i < 2; ++i) {
            const float x = at[i].x, y = at[i].y;
            if (on[i]) {
                auto gr = radial(x, y, 66.5f);
                const auto c = tint(juce::Colour(0xffe2364f));
                gr.addColour(0.0, c.withAlpha(0.34f));
                gr.addColour(0.64, c.withAlpha(0.0f));
                gr.addColour(1.0, c.withAlpha(0.0f));
                g.setGradientFill(gr);
                g.fillEllipse(x - 47, y - 47, 94, 94);
            }
            blitImage(g, on[i] ? ledOn_ : ledOff_, x - 18, y - 18);
            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(static_cast<float>(-30.0 * kPi / 180.0), x - 13 + 10.5f, y - 13 + 8.5f));
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.fillEllipse(x - 13 + 8, y - 13 + 7, 5, 3);
            g.restoreState();
        }
    }
    // ---- the footswitch cap ----
    blit(g, footPressed_ ? atlas::foot_cap_down : atlas::foot_cap_up, kFoot.x - 70, kFoot.y - 70);
}

void PedalView::paint(juce::Graphics& g) {
    const double phys = g.getInternalContext().getPhysicalPixelScaleFactor();
    ensureCaches(phys);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(staticLayer_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    g.saveState();
    g.addTransform(juce::AffineTransform::scale(scale_));
    g.addTransform(juce::AffineTransform::translation(kPedalX, kPedalY));
    paintDynamic(g);
    g.restoreState();
}

// =================================================================================================== mouse
juce::Point<float> PedalView::toPedal(juce::Point<float> p) const {
    return {p.x / scale_ - kPedalX, p.y / scale_ - kPedalY};
}

PedalView::Hit PedalView::hitTest(juce::Point<float> d) const {
    Hit h;
    for (int i = 0; i < 9; ++i)
        if (d.getDistanceFrom({kRot[i].x, kRot[i].y}) <= kRot[i].hit) { h.kind = Hit::Rotary; h.index = i; return h; }
    for (int s = 0; s < 6; ++s)
        if (std::abs(d.x - kSide[s].x) <= 32.0f && std::abs(d.y - kSide[s].y) <= 34.0f) { h.kind = Hit::Side; h.index = s; return h; }
    for (int t = 0; t < 2; ++t)
        if (std::abs(d.x - kSel[t].x) <= 32.0f && std::abs(d.y - kSel[t].y) <= 34.0f) { h.kind = Hit::Select; h.index = t; return h; }
    if (std::abs(d.x - kPreset.x) <= 62.0f && std::abs(d.y - kPreset.y) <= 26.0f) { h.kind = Hit::Select; h.index = 2; return h; }
    if (d.getDistanceFrom({kFoot.x, kFoot.y}) <= 40.0f) { h.kind = Hit::Foot; return h; }
    return h;
}

void PedalView::mouseMove(const juce::MouseEvent& e) {
    const auto h = hitTest(toPedal(e.position));
    setMouseCursor(h.kind == Hit::Rotary ? juce::MouseCursor::UpDownResizeCursor
                   : h.kind == Hit::None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    const int hk = h.kind == Hit::Rotary ? h.index : -1;
    if (hk != hoverRot_) {
        hoverRot_ = hk;
        repaint();
    }
}

void PedalView::mouseExit(const juce::MouseEvent&) {
    if (hoverRot_ != -1) {
        hoverRot_ = -1;
        repaint();
    }
}

// right-click a control: assign a MIDI controller knob to it
void PedalView::showMidiMenu(Hit hit, juce::Point<int> where) {
    const char* id = nullptr;
    if (hit.kind == Hit::Rotary) id = kRot[hit.index].id;
    else if (hit.kind == Hit::Side) id = kSide[hit.index].id;
    else if (hit.kind == Hit::Select && hit.index < 2) id = kSel[hit.index].id;
    else if (hit.kind == Hit::Foot) id = ids::power;
    auto* p = id != nullptr ? param(id) : nullptr;
    if (p == nullptr) return;
    const int cc = proc_.midiCcOf(p);
    juce::PopupMenu m;
    m.addItem(1, "Learn MIDI for " + p->getName(40) + "  (then move your controller)");
    if (cc >= 0) m.addItem(2, "Forget MIDI CC " + juce::String(cc));
    if (proc_.midiLearnTarget() != nullptr) m.addItem(3, "Cancel MIDI learn");
    juce::Component::SafePointer<PedalView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(where.x, where.y, 1, 1)).withParentComponent(nullptr),
                    [safe, p](int r) {
                        if (safe == nullptr) return;
                        if (r == 1) safe->proc_.startMidiLearn(p);
                        else if (r == 2) safe->proc_.clearMidiMapping(p);
                        else if (r == 3) safe->proc_.cancelMidiLearn();
                    });
}

void PedalView::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        drag_ = {};
        showMidiMenu(hitTest(toPedal(e.position)), e.getScreenPosition());
        return;
    }
    drag_ = hitTest(toPedal(e.position));
    lastDragPos_ = e.position;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (drag_.kind == Hit::Rotary) {
        if (auto* p = param(kRot[drag_.index].id)) p->beginChangeGesture();
        dragValue_ = rotValue(drag_.index);
    } else if (drag_.kind == Hit::Side) {
        // flips at once (so it can be played fast); if you keep holding, it flips back when you let go
        const auto s = static_cast<size_t>(drag_.index);
        sidePrev_[s] = sideOn(drag_.index);
        sideDownAt_[s] = now;
        setSide(drag_.index, !sidePrev_[s]);
        repaint();
    } else if (drag_.kind == Hit::Select) {
        const auto d = toPedal(e.position);
        if (drag_.index == 2) {
            const float dx = d.x - kPreset.x;
            setSelectPos(2, dx < -14.0f ? 0 : (dx > 14.0f ? 2 : 1));
        } else if (drag_.index == 0) {
            setSelectPos(0, selectPos(0) == 0 ? 2 : 0);
        } else {
            const int cur = selectPos(1);
            setSelectPos(1, d.y < kSel[1].y ? cur - 1 : cur + 1);
        }
        repaint();
    } else if (drag_.kind == Hit::Foot) {
        footPressed_ = true;
        footPrev_ = boolParam(ids::power);
        footDownAt_ = now;
        setFoot(!footPrev_);
        repaint();
    }
}

void PedalView::mouseDrag(const juce::MouseEvent& e) {
    if (drag_.kind != Hit::Rotary) return;
    const auto delta = e.position - lastDragPos_;
    lastDragPos_ = e.position;
    // up or to the right turns it up; 220 design pixels is the whole range
    float change = ((-delta.y) + delta.x) / (220.0f * scale_);
    if (e.mods.isShiftDown()) change *= 0.2f;
    dragValue_ = clamp01(dragValue_ + change);
    setRot(drag_.index, dragValue_);
    repaint();
}

void PedalView::mouseUp(const juce::MouseEvent&) {
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (drag_.kind == Hit::Rotary) {
        if (auto* p = param(kRot[drag_.index].id)) p->endChangeGesture();
    } else if (drag_.kind == Hit::Side) {
        const auto s = static_cast<size_t>(drag_.index);
        if (now - sideDownAt_[s] >= kHoldMs) setSide(drag_.index, sidePrev_[s]); // held: momentary
        repaint();
    } else if (drag_.kind == Hit::Foot) {
        if (now - footDownAt_ >= kHoldMs) setFoot(footPrev_);
        footPressed_ = false;
        repaint();
    }
    drag_ = {};
}

void PedalView::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto h = hitTest(toPedal(e.position));
    if (h.kind != Hit::Rotary) return;
    if (auto* p = param(kRot[h.index].id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getDefaultValue());
        p->endChangeGesture();
        dragValue_ = p->getDefaultValue();
        repaint();
    }
}

void PedalView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const auto h = hitTest(toPedal(e.position));
    const float dy = w.isReversed ? -w.deltaY : w.deltaY;
    if (h.kind == Hit::Rotary) {
        auto* p = param(kRot[h.index].id);
        if (p == nullptr) return;
        if (kRot[h.index].steps > 1) { // a selector: one step per notch
            wheelAccum_ += dy;
            if (std::abs(wheelAccum_) >= 0.25f) {
                const int step = wheelAccum_ > 0 ? 1 : -1;
                wheelAccum_ = 0;
                p->beginChangeGesture();
                setRot(h.index, rotValue(h.index) + static_cast<float>(step) / static_cast<float>(kRot[h.index].steps - 1));
                p->endChangeGesture();
            }
        } else {
            p->beginChangeGesture();
            setRot(h.index, rotValue(h.index) + juce::jlimit(-0.06f, 0.06f, dy * 0.12f));
            p->endChangeGesture();
        }
        repaint();
    } else if (h.kind == Hit::Side) {
        wheelAccum_ += dy;
        if (std::abs(wheelAccum_) >= 0.25f) {
            setSide(h.index, wheelAccum_ > 0); // wheel up = lever up = on
            wheelAccum_ = 0;
            repaint();
        }
    } else if (h.kind == Hit::Select) {
        wheelAccum_ += dy;
        if (std::abs(wheelAccum_) >= 0.25f) {
            const int step = wheelAccum_ > 0 ? -1 : 1; // wheel up = lever up
            wheelAccum_ = 0;
            setSelectPos(h.index, selectPos(h.index) + step);
            repaint();
        }
    }
}

// =================================================================================================== housekeeping
void PedalView::timerCallback() {
    // repaint when something the picture shows has changed (a knob moved by the host, a preset loaded, ...)
    std::array<float, 48> now{};
    for (int i = 0; i < 9; ++i) now[static_cast<size_t>(i)] = rotValue(i);
    for (int s = 0; s < 6; ++s) now[static_cast<size_t>(9 + s)] = sideOn(s) ? 1.0f : 0.0f;
    for (int t = 0; t < 3; ++t) now[static_cast<size_t>(15 + t)] = static_cast<float>(selectIndex(t));
    now[18] = boolParam(ids::power) ? 1.0f : 0.0f;
    now[19] = footPressed_ ? 1.0f : 0.0f;
    if (now != shown_) {
        shown_ = now;
        repaint();
        return;
    }
    // the lights behind the side levers move with the sound: repaint just that spot when they change
    for (int s = 0; s < 6; ++s) {
        const int key = sideOn(s) ? 1 + juce::roundToInt(sideActivity(s) * 10.0f) : 0;
        if (key != glowKey_[static_cast<size_t>(s)]) {
            glowKey_[static_cast<size_t>(s)] = key;
            repaint(juce::Rectangle<float>(kPedalX + kSide[s].x - 56.0f, kPedalY + kSide[s].y - 56.0f, 112.0f, 112.0f)
                        .transformedBy(juce::AffineTransform::scale(scale_)).getSmallestIntegerContainer());
        }
    }
    // the wordmark's fuzzy edge moves with the sound: repaint just that strip when it changes
    const int key = juce::roundToInt(scope_->level() * 24.0f) * 16 + juce::roundToInt(scope_->clipping() * 8.0f);
    if (key != wordKey_) {
        wordKey_ = key;
        repaint(wordmarkArea().transformedBy(juce::AffineTransform::scale(scale_)).getSmallestIntegerContainer());
    }
}
