#include "ScopeView.h"

#include <cmath>

#include "Assets.h"
#include "ParamIds.h"

namespace {
inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float follow(float cur, float target, float up, float down) { return cur + (target - cur) * (target > cur ? up : down); }
inline float mapDb(float amp, float floorDb, float rangeDb) {
    return clamp01((20.0f * std::log10(amp + 1.0e-6f) - floorDb) / rangeDb);
}
constexpr float kXyX = 36.0f, kXyY = 16.0f, kXyS = 144.0f;       // the transfer-curve box
constexpr float kScX = 8.0f, kScY = 182.0f, kScW = 200.0f, kScH = 138.0f; // the waveform box
constexpr float kStatusY = 330.0f;
} // namespace

ScopeView::ScopeView(VerdigrisProcessor& p) : proc_(p) {
    setOpaque(true);
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

void ScopeView::setLook(juce::Colour accent, juce::Colour deep) {
    accent_ = accent;
    deep_ = deep;
    repaint();
}

void ScopeView::setClip(VideoClipPtr clip) {
    clip_ = std::move(clip);
    clipStart_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
    repaint();
}

// ------------------------------------------------------------------------------------------------ meters
void ScopeView::readMeters() {
    const float vo = proc_.visOut.exchange(0.0f, std::memory_order_relaxed);
    const float vi = proc_.visIn.exchange(0.0f, std::memory_order_relaxed);
    level_ = follow(level_, mapDb(vo, -48.0f, 46.0f), 0.55f, 0.12f);
    inLevel_ = follow(inLevel_, mapDb(vi, -48.0f, 46.0f), 0.55f, 0.12f);
    clipAct_ = follow(clipAct_, clamp01(proc_.uiClip.load(std::memory_order_relaxed)), 0.5f, 0.15f);
    supply_ = proc_.uiSupply.load(std::memory_order_relaxed);
    swell_ = proc_.uiSwell.load(std::memory_order_relaxed);
    chop_ = follow(chop_, proc_.uiChop.load(std::memory_order_relaxed), 0.6f, 0.4f);
    chopStep_ = proc_.uiChopStep.load(std::memory_order_relaxed);
    const double beat = proc_.uiBeat.load(std::memory_order_relaxed);
    beatPulse_ = std::exp(-static_cast<float>(beat - std::floor(beat)) * 5.0f);
    flash_ *= 0.85f;
    if (clipAct_ > 0.6f) flash_ = juce::jmax(flash_, clipAct_ * 0.5f);
}

// copy the newest columns of the audio thread's scope, line the input up with the output (the circuit's delay), and find a
// rising zero crossing to start the trace on so a steady note stands still
void ScopeView::readScope() {
    constexpr int M = kCols * 2;
    const unsigned head = proc_.scopeHead.load(std::memory_order_acquire);
    const int N = VerdigrisProcessor::kScopeCols;
    const int lat = proc_.scopeLatencyCols.load(std::memory_order_relaxed);
    static thread_local float oMin[M], oMax[M], iMin[M], iMax[M];
    for (int j = 0; j < M; ++j) {
        const size_t k = (head + static_cast<unsigned>(N * 4 - M + j)) % static_cast<unsigned>(N);
        oMin[j] = proc_.scopeOutMin[k].load(std::memory_order_relaxed);
        oMax[j] = proc_.scopeOutMax[k].load(std::memory_order_relaxed);
        iMin[j] = proc_.scopeInMin[k].load(std::memory_order_relaxed);
        iMax[j] = proc_.scopeInMax[k].load(std::memory_order_relaxed);
    }
    // the trigger
    float dc = 0.0f, pk = 0.0f;
    for (int j = M - kCols; j < M; ++j) dc += 0.5f * (oMin[j] + oMax[j]);
    dc /= kCols;
    for (int j = M - kCols; j < M; ++j) pk = juce::jmax(pk, juce::jmax(std::abs(oMax[j] - dc), std::abs(oMin[j] - dc)));
    int start = M - kCols;
    if (pk > 0.004f)
        for (int i = M - kCols - 1; i > 8; --i) {
            const float a = 0.5f * (oMin[i - 1] + oMax[i - 1]) - dc, b = 0.5f * (oMin[i] + oMax[i]) - dc;
            if (a < 0.0f && b >= 0.0f) { start = i; break; }
        }
    float oPk = 0.0f, iPk = 0.0f;
    for (int c = 0; c < kCols; ++c) {
        const int j = start + c, ji = juce::jmax(0, j - lat);
        outMin_[c] = oMin[j] - dc;
        outMax_[c] = oMax[j] - dc;
        inMin_[c] = iMin[ji];
        inMax_[c] = iMax[ji];
        oPk = juce::jmax(oPk, juce::jmax(std::abs(outMin_[c]), std::abs(outMax_[c])));
        iPk = juce::jmax(iPk, juce::jmax(std::abs(inMin_[c]), std::abs(inMax_[c])));
    }
    // the transfer curve uses the newest columns (not the triggered ones) so it never lags
    for (int c = 0; c < kCols; ++c) {
        const int j = M - kCols + c, ji = juce::jmax(0, j - lat);
        xyOut_[c] = 0.5f * (oMin[j] + oMax[j]) - dc;
        xyIn_[c] = 0.5f * (iMin[ji] + iMax[ji]);
    }
    // gentle auto-scaling: the trace always fills the box, but never blows silence up into noise
    outScale_ = follow(outScale_, 0.92f / juce::jmax(oPk * 1.05f, 0.06f), 0.04f, 0.4f);
    inScale_ = follow(inScale_, 0.92f / juce::jmax(iPk * 1.05f, 0.02f), 0.04f, 0.4f);
}

void ScopeView::timerCallback() {
    if (!still_) {
        now_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
        readMeters();
        readScope();
    }
    if (isShowing()) repaint();
}

// ------------------------------------------------------------------------------------------------ drawing
void ScopeView::drawBackdrop(juce::Graphics& g) const {
    juce::ColourGradient bg(deep_.interpolatedWith(accent_, 0.05f).darker(0.2f), 0, 0, deep_.interpolatedWith(accent_, 0.12f), 0, kH, false);
    g.setGradientFill(bg);
    g.fillRect(0.0f, 0.0f, kW, kH);
    // the halo breathes with the output
    const float halo = 0.04f + 0.30f * level_ + 0.3f * flash_;
    juce::ColourGradient gr(accent_.withAlpha(halo), kW * 0.5f, kScY + kScH * 0.5f, accent_.withAlpha(0.0f), kW * 0.5f, kScY - 120.0f, true);
    g.setGradientFill(gr);
    g.fillEllipse(kW * 0.5f - 190.0f, kScY + kScH * 0.5f - 190.0f, 380.0f, 380.0f);
    // phosphor grids
    const auto grid = accent_.withAlpha(0.10f);
    g.setColour(grid);
    for (int i = 0; i <= 8; ++i) {
        const float t = kXyS * static_cast<float>(i) / 8.0f;
        g.drawLine(kXyX + t, kXyY, kXyX + t, kXyY + kXyS, i == 4 ? 1.0f : 0.5f);
        g.drawLine(kXyX, kXyY + t, kXyX + kXyS, kXyY + t, i == 4 ? 1.0f : 0.5f);
    }
    for (int i = 0; i <= 10; ++i) g.drawLine(kScX + kScW * static_cast<float>(i) / 10.0f, kScY, kScX + kScW * static_cast<float>(i) / 10.0f, kScY + kScH, 0.5f);
    for (int i = 0; i <= 4; ++i) g.drawLine(kScX, kScY + kScH * static_cast<float>(i) / 4.0f, kScX + kScW, kScY + kScH * static_cast<float>(i) / 4.0f, i == 2 ? 1.0f : 0.5f);
    g.setColour(accent_.withAlpha(0.30f));
    g.drawRect(kXyX, kXyY, kXyS, kXyS, 1.0f);
    g.drawRect(kScX, kScY, kScW, kScH, 1.0f);
    // labels
    g.setColour(accent_.interpolatedWith(juce::Colours::white, 0.3f).withAlpha(0.55f));
    g.setFont(assets::font(500, 8.5f));
    g.drawText("IN", juce::Rectangle<float>(kXyX + kXyS - 22.0f, kXyY + kXyS + 1.0f, 22.0f, 10.0f), juce::Justification::centredRight);
    g.drawText("OUT", juce::Rectangle<float>(kXyX - 30.0f, kXyY, 26.0f, 10.0f), juce::Justification::centredRight);
    g.drawText("GUITAR", juce::Rectangle<float>(kScX + 4.0f, kScY + 3.0f, 60.0f, 10.0f), juce::Justification::centredLeft);
    g.drawText("FUZZ", juce::Rectangle<float>(kScX + 4.0f, kScY + 12.0f, 60.0f, 10.0f), juce::Justification::centredLeft);
}

void ScopeView::drawTransfer(juce::Graphics& g) const {
    const float cx = kXyX + kXyS * 0.5f, cy = kXyY + kXyS * 0.5f, half = kXyS * 0.5f;
    // the reference diagonal (what a clean pedal would do)
    g.setColour(accent_.withAlpha(0.18f));
    g.drawLine(cx - half, cy + half, cx + half, cy - half, 0.8f);
    // the rails: the top and bottom the transistors cannot go past
    const float rail = 0.06f + 0.5f * clipAct_;
    g.setColour(accent_.withAlpha(rail));
    g.drawLine(kXyX, kXyY + 2.0f, kXyX + kXyS, kXyY + 2.0f, 1.2f);
    g.drawLine(kXyX, kXyY + kXyS - 2.0f, kXyX + kXyS, kXyY + kXyS - 2.0f, 1.2f);

    juce::Path p;
    bool first = true;
    for (int c = 0; c < kCols; ++c) {
        const float x = cx + juce::jlimit(-1.0f, 1.0f, xyIn_[c] * inScale_) * (half - 3.0f);
        const float y = cy - juce::jlimit(-1.0f, 1.0f, xyOut_[c] * outScale_) * (half - 3.0f);
        if (first) { p.startNewSubPath(x, y); first = false; } else p.lineTo(x, y);
    }
    const float a = clamp01(0.25f + level_ * 1.5f);
    g.setColour(accent_.withAlpha(0.10f * a));
    g.strokePath(p, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accent_.withAlpha(0.35f * a));
    g.strokePath(p, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.interpolatedWith(accent_, 0.2f).withAlpha(clamp01(0.3f + 0.7f * a)));
    g.strokePath(p, juce::PathStrokeType(1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // the operating point: where the signal is right now
    const float px = cx + juce::jlimit(-1.0f, 1.0f, xyIn_[kCols - 1] * inScale_) * (half - 3.0f);
    const float py = cy - juce::jlimit(-1.0f, 1.0f, xyOut_[kCols - 1] * outScale_) * (half - 3.0f);
    juce::ColourGradient dot(juce::Colours::white.withAlpha(0.9f), px, py, accent_.withAlpha(0.0f), px + 9.0f, py, true);
    g.setGradientFill(dot);
    g.fillEllipse(px - 9.0f, py - 9.0f, 18.0f, 18.0f);
}

void ScopeView::drawScope(juce::Graphics& g, bool overVideo) const {
    const float cy = kScY + kScH * 0.5f, half = kScH * 0.5f - 4.0f;
    auto yOf = [&](float v, float scale) { return cy - juce::jlimit(-1.0f, 1.0f, v * scale) * half; };
    if (overVideo) { // a dark plate so the trace reads over any picture
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(kScX, kScY, kScW, kScH);
    }
    const float dx = kScW / static_cast<float>(kCols - 1);
    // your guitar, dim
    {
        juce::Path top;
        for (int c = 0; c < kCols; ++c) {
            const float x = kScX + dx * static_cast<float>(c);
            if (c == 0) top.startNewSubPath(x, yOf(0.5f * (inMin_[c] + inMax_[c]), inScale_));
            else top.lineTo(x, yOf(0.5f * (inMin_[c] + inMax_[c]), inScale_));
        }
        g.setColour(juce::Colours::white.withAlpha(overVideo ? 0.30f : 0.22f));
        g.strokePath(top, juce::PathStrokeType(1.0f));
    }
    // the fuzz: a ribbon from the lowest to the highest sample of each column, with a bright edge
    {
        juce::Path ribbon, edge;
        for (int c = 0; c < kCols; ++c) {
            const float x = kScX + dx * static_cast<float>(c);
            if (c == 0) ribbon.startNewSubPath(x, yOf(outMax_[c], outScale_)); else ribbon.lineTo(x, yOf(outMax_[c], outScale_));
        }
        for (int c = kCols - 1; c >= 0; --c) ribbon.lineTo(kScX + dx * static_cast<float>(c), yOf(outMin_[c], outScale_));
        ribbon.closeSubPath();
        for (int c = 0; c < kCols; ++c) {
            const float x = kScX + dx * static_cast<float>(c);
            const float y = yOf(0.5f * (outMin_[c] + outMax_[c]), outScale_);
            if (c == 0) edge.startNewSubPath(x, y); else edge.lineTo(x, y);
        }
        const float a = clamp01(0.3f + level_ * 1.4f);
        g.setColour(accent_.withAlpha(0.10f * a));
        g.strokePath(edge, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent_.withAlpha(0.38f * a));
        g.fillPath(ribbon);
        g.setColour(accent_.withAlpha(0.45f * a));
        g.strokePath(edge, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colours::white.interpolatedWith(accent_, 0.25f).withAlpha(clamp01(0.35f + 0.65f * a)));
        g.strokePath(edge, juce::PathStrokeType(1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // the rails flash when the transistors run out of room
    if (clipAct_ > 0.05f) {
        const float a = clamp01(clipAct_ * 1.3f);
        const juce::Colour hot = accent_.interpolatedWith(juce::Colours::white, 0.5f);
        g.setColour(hot.withAlpha(0.20f * a));
        g.fillRect(kScX, kScY, kScW, 3.0f);
        g.fillRect(kScX, kScY + kScH - 3.0f, kScW, 3.0f);
        g.setColour(hot.withAlpha(0.7f * a));
        g.drawLine(kScX, kScY + 1.0f, kScX + kScW, kScY + 1.0f, 1.0f);
        g.drawLine(kScX, kScY + kScH - 1.0f, kScX + kScW, kScY + kScH - 1.0f, 1.0f);
    }
}

void ScopeView::drawStatus(juce::Graphics& g, bool overVideo) const {
    if (overVideo) {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(0.0f, kStatusY - 4.0f, kW, kH - kStatusY + 4.0f);
    }
    const auto ink = accent_.interpolatedWith(juce::Colours::white, 0.35f);
    auto label = [&](const juce::String& t, float y) {
        g.setColour(ink.withAlpha(0.6f));
        g.setFont(assets::font(500, 8.5f));
        g.drawText(t, juce::Rectangle<float>(10.0f, y - 1.0f, 44.0f, 11.0f), juce::Justification::centredLeft);
    };
    auto bar = [&](float x, float y, float w, float frac, float alpha) {
        g.setColour(accent_.withAlpha(0.12f));
        g.fillRoundedRectangle(x, y, w, 7.0f, 3.5f);
        const float f = clamp01(frac);
        if (f > 0.01f) {
            g.setColour(accent_.withAlpha(alpha));
            g.fillRoundedRectangle(x, y, juce::jmax(7.0f, w * f), 7.0f, 3.5f);
            g.setColour(juce::Colours::white.withAlpha(0.25f * alpha));
            g.fillRoundedRectangle(x + 2.0f, y + 1.0f, juce::jmax(3.0f, w * f - 4.0f), 2.0f, 1.0f);
        }
    };
    // supply
    label("SUPPLY", kStatusY);
    bar(54.0f, kStatusY + 1.0f, 108.0f, supply_ / 9.6f, 0.85f);
    g.setColour(ink.withAlpha(0.85f));
    g.setFont(assets::font(500, 9.5f));
    g.drawText(juce::String(supply_, 1) + " V", juce::Rectangle<float>(166.0f, kStatusY - 2.0f, 42.0f, 11.0f), juce::Justification::centredRight);
    // chop pattern
    label("CHOP", kStatusY + 18.0f);
    {
        const bool chopOn = proc_.apvts.getRawParameterValue(ids::chop)->load() > 0.5f;
        const int pat = static_cast<int>(proc_.apvts.getRawParameterValue(ids::chopPattern)->load() + 0.5f);
        const auto& def = verdigris::patternDef(pat);
        const int cells = def.randomChance > 0.0f ? 16 : def.len;
        const float cw = 8.0f, gap = 2.0f, total = cells * cw + (cells - 1) * gap;
        const float x00 = 54.0f + juce::jmax(0.0f, (156.0f - total) * 0.5f);
        const int cur = cells > 0 ? ((chopStep_ % cells) + cells) % cells : 0;
        for (int i = 0; i < cells; ++i) {
            const float x = x00 + static_cast<float>(i) * (cw + gap), y = kStatusY + 18.0f;
            const float v = def.randomChance > 0.0f ? 0.3f : def.v[i];
            g.setColour(accent_.withAlpha(0.10f + (chopOn ? 0.30f : 0.10f) * v));
            g.fillRoundedRectangle(x, y, cw, 8.0f, 1.5f);
            if (chopOn && i == cur) {
                g.setColour(accent_.interpolatedWith(juce::Colours::white, 0.4f).withAlpha(0.35f + 0.65f * chop_));
                g.fillRoundedRectangle(x, y, cw, 8.0f, 1.5f);
                g.setColour(accent_.withAlpha(0.25f * chop_));
                g.fillRoundedRectangle(x - 2.0f, y - 2.0f, cw + 4.0f, 12.0f, 3.0f);
            }
        }
    }
    // swell
    label("SWELL", kStatusY + 36.0f);
    {
        const bool swellOn = proc_.apvts.getRawParameterValue(ids::swell)->load() > 0.5f;
        bar(54.0f, kStatusY + 37.0f, 154.0f, swellOn ? swell_ : 1.0f, swellOn ? 0.85f : 0.25f);
    }
}

void ScopeView::drawGlassOverlay(juce::Graphics& g) const {
    const float edge = 20.0f;
    for (int side = 0; side < 4; ++side) {
        juce::Rectangle<float> r;
        juce::ColourGradient gr;
        switch (side) {
            case 0: r = {0, 0, kW, edge};         gr = juce::ColourGradient(juce::Colours::black.withAlpha(0.65f), 0, 0, juce::Colours::transparentBlack, 0, edge, false); break;
            case 1: r = {0, kH - edge, kW, edge}; gr = juce::ColourGradient(juce::Colours::black.withAlpha(0.65f), 0, kH, juce::Colours::transparentBlack, 0, kH - edge, false); break;
            case 2: r = {0, 0, edge, kH};         gr = juce::ColourGradient(juce::Colours::black.withAlpha(0.55f), 0, 0, juce::Colours::transparentBlack, edge, 0, false); break;
            default: r = {kW - edge, 0, edge, kH}; gr = juce::ColourGradient(juce::Colours::black.withAlpha(0.55f), kW, 0, juce::Colours::transparentBlack, kW - edge, 0, false); break;
        }
        g.setGradientFill(gr);
        g.fillRect(r);
    }
    juce::Path sheen;
    sheen.addTriangle(0, 0, kW * 0.9f, 0, 0, kH * 0.42f);
    juce::ColourGradient sg(juce::Colours::white.withAlpha(0.07f), 0, 0, juce::Colours::white.withAlpha(0.0f), kW * 0.5f, kH * 0.2f, false);
    g.setGradientFill(sg);
    g.fillPath(sheen);
}

void ScopeView::paint(juce::Graphics& g) {
    const float s = getWidth() / kW;
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    juce::Path clip;
    clip.addRoundedRectangle(0.0f, 0.0f, static_cast<float>(getWidth()), static_cast<float>(getHeight()), 14.0f * s);
    g.fillAll(juce::Colour(0xff030405));
    g.reduceClipRegion(clip);
    g.addTransform(juce::AffineTransform::scale(s));

    const bool haveClip = clip_ != nullptr && !clip_->frames.empty();
    if (haveClip) {
        const double t = now_ - clipStart_;
        const auto& frame = clip_->frames[static_cast<size_t>(clip_->frameAt(t))];
        g.setColour(juce::Colours::black);
        g.fillRect(0.0f, 0.0f, kW, kH);
        g.drawImage(frame, juce::Rectangle<float>(0, 0, kW, kH), juce::RectanglePlacement::stretchToFit);
        // the picture breathes with the sound: dim when quiet, brighter and tinted when the fuzz roars
        const float bright = clamp01(0.40f + 0.50f * level_ + 0.25f * inLevel_ + 0.3f * flash_);
        g.setColour(juce::Colours::black.withAlpha(clamp01(1.0f - bright)));
        g.fillRect(0.0f, 0.0f, kW, kH);
        g.setColour(accent_.withAlpha(0.05f + 0.16f * level_ + 0.10f * beatPulse_ * (1.0f - chop_ * 0.5f) + 0.2f * flash_));
        g.fillRect(0.0f, 0.0f, kW, kH);
        drawScope(g, true);
        drawStatus(g, true);
    } else {
        drawBackdrop(g);
        drawTransfer(g);
        drawScope(g, false);
        drawStatus(g, false);
    }
    drawGlassOverlay(g);
}
