#include "Assets.h"

#include <BinaryData.h>

#include <cmath>
#include <vector>

namespace assets {
namespace {

// Find an embedded file by the name it had on disk (the generated identifiers differ from the file names).
const char* findResource(const juce::String& fileName, int& size) {
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        const char* id = BinaryData::namedResourceList[i];
        if (juce::String(BinaryData::getNamedResourceOriginalFilename(id)) == fileName)
            return BinaryData::getNamedResource(id, size);
    }
    size = 0;
    return nullptr;
}

juce::Image loadImage(const juce::String& fileName) {
    int size = 0;
    const char* data = findResource(fileName, size);
    jassert(data != nullptr);
    if (data == nullptr) return {};
    auto img = juce::ImageFileFormat::loadFrom(data, static_cast<size_t>(size));
    return img.convertedToFormat(juce::Image::ARGB);
}

juce::Typeface::Ptr loadFont(const juce::String& fileName) {
    int size = 0;
    const char* data = findResource(fileName, size);
    jassert(data != nullptr);
    if (data == nullptr) return {};
    return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
}

struct Taps {
    std::vector<int> index;    // first source pixel of each destination pixel
    std::vector<int> count;
    std::vector<float> weight; // concatenated weights
    std::vector<size_t> start;
};

Taps makeTaps(int srcLen, int dstLen) {
    Taps t;
    t.index.resize(static_cast<size_t>(dstLen));
    t.count.resize(static_cast<size_t>(dstLen));
    t.start.resize(static_cast<size_t>(dstLen));
    const double k = static_cast<double>(srcLen) / dstLen;
    for (int d = 0; d < dstLen; ++d) {
        const double x0 = d * k, x1 = (d + 1) * k;
        const int i0 = static_cast<int>(std::floor(x0));
        const int i1 = juce::jmin(srcLen - 1, static_cast<int>(std::ceil(x1)) - 1);
        t.index[static_cast<size_t>(d)] = i0;
        t.count[static_cast<size_t>(d)] = i1 - i0 + 1;
        t.start[static_cast<size_t>(d)] = t.weight.size();
        double total = 0;
        for (int i = i0; i <= i1; ++i) {
            const double w = juce::jmin(static_cast<double>(i + 1), x1) - juce::jmax(static_cast<double>(i), x0);
            t.weight.push_back(static_cast<float>(juce::jmax(0.0, w)));
            total += juce::jmax(0.0, w);
        }
        if (total > 0)
            for (int i = 0; i <= i1 - i0; ++i) t.weight[t.start[static_cast<size_t>(d)] + static_cast<size_t>(i)] /= static_cast<float>(total);
    }
    return t;
}

} // namespace


// ------------------------------------------------------------------------------------------------ textures
// The body textures are made in code, once, the first time they are needed: grey maps (1120 x 1920) that Body.cpp lays
// over the colour. 0 Hammered, 1 Crinkle (wrinkle finish), 2 Flake (metal flake), 3 Brushed.
namespace {

inline uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float rnd01(int x, int y, uint32_t seed) { return static_cast<float>(hash2(x, y, seed) & 0xffffff) / 16777215.0f; }
inline float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// smooth value noise; sx, sy are the feature sizes in pixels
float vnoise(float x, float y, float sx, float sy, uint32_t seed) {
    const float fx = x / sx, fy = y / sy;
    const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
    const float tx = smooth(fx - ix), ty = smooth(fy - iy);
    const float a = rnd01(ix, iy, seed), b = rnd01(ix + 1, iy, seed), c = rnd01(ix, iy + 1, seed), d = rnd01(ix + 1, iy + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

float fbm(float x, float y, float size, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * vnoise(x, y, size, size, seed + static_cast<uint32_t>(o) * 17u);
        norm += amp;
        amp *= 0.5f;
        size *= 0.5f;
    }
    return sum / norm;
}

juce::Image makeTexture(int id) {
    constexpr int W = 1120, H = 1920;
    std::vector<float> v(static_cast<size_t>(W) * H);
    auto at = [&](int x, int y) -> float& { return v[static_cast<size_t>(y) * W + static_cast<size_t>(x)]; };
    switch (id) {
        case 3: // brushed: long horizontal streaks
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const float n = 0.45f * vnoise(static_cast<float>(x), static_cast<float>(y), 260.0f, 1.6f, 11) +
                                    0.35f * vnoise(static_cast<float>(x), static_cast<float>(y), 90.0f, 1.0f, 23) +
                                    0.20f * vnoise(static_cast<float>(x), static_cast<float>(y), 30.0f, 0.7f, 37);
                    at(x, y) = 0.5f + (n - 0.5f) * 1.5f;
                }
            break;
        case 0: { // hammered: a field of shallow dimples, lit from the top left
            constexpr int cell = 30;
            std::vector<float> h(static_cast<size_t>(W) * H);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const int cx = x / cell, cy = y / cell;
                    float best = 1.0e9f;
                    for (int j = -1; j <= 1; ++j)
                        for (int i = -1; i <= 1; ++i) {
                            const float px = (cx + i + 0.15f + 0.7f * rnd01(cx + i, cy + j, 5)) * cell;
                            const float py = (cy + j + 0.15f + 0.7f * rnd01(cx + i, cy + j, 9)) * cell;
                            const float dx = x - px, dy = y - py;
                            best = juce::jmin(best, dx * dx + dy * dy);
                        }
                    const float d = std::sqrt(best) / (cell * 0.62f);
                    h[static_cast<size_t>(y) * W + static_cast<size_t>(x)] = d < 1.0f ? -(1.0f - d * d) : 0.0f; // a bowl
                }
            for (int y = 1; y < H; ++y)
                for (int x = 1; x < W; ++x) {
                    const float gx = h[static_cast<size_t>(y) * W + static_cast<size_t>(x)] - h[static_cast<size_t>(y) * W + static_cast<size_t>(x - 1)];
                    const float gy = h[static_cast<size_t>(y) * W + static_cast<size_t>(x)] - h[static_cast<size_t>(y - 1) * W + static_cast<size_t>(x)];
                    at(x, y) = 0.5f + (gx + gy) * 1.9f + 0.05f * (vnoise(static_cast<float>(x), static_cast<float>(y), 3.0f, 3.0f, 3) - 0.5f);
                }
            for (int x = 0; x < W; ++x) at(x, 0) = 0.5f;
            for (int y = 0; y < H; ++y) at(0, y) = 0.5f;
            break;
        }
        case 1: { // crinkle: the wrinkled ridges of a wrinkle-finish paint, lit from the top left
            std::vector<float> h(static_cast<size_t>(W) * H);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
                    const float wx = fx + 38.0f * (fbm(fx, fy, 170.0f, 3, 101) - 0.5f);
                    const float wy = fy + 38.0f * (fbm(fx, fy, 170.0f, 3, 113) - 0.5f);
                    float r = 0.0f, amp = 0.6f, size = 54.0f;
                    for (int o = 0; o < 3; ++o) {
                        const float n = vnoise(wx, wy, size, size, 131 + static_cast<uint32_t>(o) * 7u);
                        r += amp * (1.0f - std::abs(2.0f * n - 1.0f)); // ridged noise: sharp creases
                        amp *= 0.5f;
                        size *= 0.5f;
                    }
                    h[static_cast<size_t>(y) * W + static_cast<size_t>(x)] = r;
                }
            for (int y = 1; y < H; ++y)
                for (int x = 1; x < W; ++x) {
                    const float gx = h[static_cast<size_t>(y) * W + static_cast<size_t>(x)] - h[static_cast<size_t>(y) * W + static_cast<size_t>(x - 1)];
                    const float gy = h[static_cast<size_t>(y) * W + static_cast<size_t>(x)] - h[static_cast<size_t>(y - 1) * W + static_cast<size_t>(x)];
                    at(x, y) = 0.5f + (gx + gy) * 2.6f + 0.04f * (vnoise(static_cast<float>(x), static_cast<float>(y), 3.0f, 3.0f, 5) - 0.5f);
                }
            for (int x = 0; x < W; ++x) at(x, 0) = 0.5f;
            for (int y = 0; y < H; ++y) at(0, y) = 0.5f;
            break;
        }
        default: { // flake: tiny tilted facets that catch the light, a few of them sparkling
            const float ca = std::cos(0.52f), sa = std::sin(0.52f);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const float u = (x * ca + y * sa) / 4.2f, w = (-x * sa + y * ca) / 4.2f;
                    const int iu = static_cast<int>(std::floor(u)), iw = static_cast<int>(std::floor(w));
                    const float tilt = rnd01(iu, iw, 21);
                    const float spark = rnd01(iu, iw, 33);
                    float val = 0.5f + (tilt - 0.5f) * 0.62f;
                    if (spark > 0.93f) val += (spark - 0.93f) * 7.0f; // the odd bright flake
                    at(x, y) = val + (fbm(static_cast<float>(x), static_cast<float>(y), 90.0f, 3, 17) - 0.5f) * 0.12f;
                }
            break;
        }
    }
    juce::Image img(juce::Image::ARGB, W, H, false);
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < H; ++y) {
        auto* line = reinterpret_cast<juce::PixelARGB*>(bd.getLinePointer(y));
        for (int x = 0; x < W; ++x) {
            const auto g = static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>(at(x, y) * 255.0f + 0.5f)));
            line[x].setARGB(255, g, g, g);
        }
    }
    return img;
}

} // namespace

const juce::Image& texture(int id) {
    static juce::Image cache[4];
    id = juce::jlimit(0, 3, id);
    if (!cache[id].isValid()) cache[id] = makeTexture(id);
    return cache[id];
}

const Raw& raw() {
    static const Raw r = [] {
        Raw x;
        x.atlas = loadImage("atlas.png");
        x.body[0] = loadImage("body_0.jpg");
        x.body[1] = loadImage("body_5.jpg");
        x.body[2] = loadImage("body_1.jpg");
        x.jost300 = loadFont("Jost-300-Light.ttf");
        x.jost400 = loadFont("Jost-400-Book.ttf");
        x.jost500 = loadFont("Jost-500-Medium.ttf");
        x.jost600 = loadFont("Jost-600-Semi.ttf");
        return x;
    }();
    return r;
}

juce::Font font(int weight, float height) {
    const auto& r = raw();
    juce::Typeface::Ptr tf = weight <= 300 ? r.jost300 : weight <= 400 ? r.jost400 : weight <= 500 ? r.jost500 : r.jost600;
    if (tf == nullptr) return juce::Font(juce::FontOptions(height));
    return juce::Font(juce::FontOptions(tf).withHeight(height));
}

juce::Image resample(const juce::Image& src, juce::Rectangle<int> region, int dw, int dh) {
    dw = juce::jmax(1, dw);
    dh = juce::jmax(1, dh);
    region = region.getIntersection(src.getBounds());
    juce::Image out(juce::Image::ARGB, dw, dh, true);
    if (region.isEmpty()) return out;

    const int sw = region.getWidth(), sh = region.getHeight();
    const Taps tx = makeTaps(sw, dw);
    const Taps ty = makeTaps(sh, dh);

    juce::Image::BitmapData in(src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData o(out, juce::Image::BitmapData::writeOnly);

    // horizontal pass: sh rows x dw columns x 4 channels
    std::vector<float> tmp(static_cast<size_t>(sh) * static_cast<size_t>(dw) * 4u);
    for (int y = 0; y < sh; ++y) {
        const uint8_t* line = in.getLinePointer(region.getY() + y) + static_cast<size_t>(region.getX()) * static_cast<size_t>(in.pixelStride);
        float* row = &tmp[static_cast<size_t>(y) * static_cast<size_t>(dw) * 4u];
        for (int x = 0; x < dw; ++x) {
            float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            const int n = tx.count[static_cast<size_t>(x)];
            const float* w = &tx.weight[tx.start[static_cast<size_t>(x)]];
            const uint8_t* p = line + static_cast<size_t>(tx.index[static_cast<size_t>(x)]) * static_cast<size_t>(in.pixelStride);
            for (int i = 0; i < n; ++i, p += in.pixelStride) {
                a0 += w[i] * p[0];
                a1 += w[i] * p[1];
                a2 += w[i] * p[2];
                a3 += w[i] * p[3];
            }
            row[x * 4 + 0] = a0;
            row[x * 4 + 1] = a1;
            row[x * 4 + 2] = a2;
            row[x * 4 + 3] = a3;
        }
    }
    // vertical pass
    for (int y = 0; y < dh; ++y) {
        uint8_t* line = o.getLinePointer(y);
        const int n = ty.count[static_cast<size_t>(y)];
        const float* w = &ty.weight[ty.start[static_cast<size_t>(y)]];
        const int i0 = ty.index[static_cast<size_t>(y)];
        for (int x = 0; x < dw; ++x) {
            float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            for (int i = 0; i < n; ++i) {
                const float* p = &tmp[(static_cast<size_t>(i0 + i) * static_cast<size_t>(dw) + static_cast<size_t>(x)) * 4u];
                a0 += w[i] * p[0];
                a1 += w[i] * p[1];
                a2 += w[i] * p[2];
                a3 += w[i] * p[3];
            }
            uint8_t* d = line + static_cast<size_t>(x) * static_cast<size_t>(o.pixelStride);
            d[0] = static_cast<uint8_t>(juce::jlimit(0, 255, static_cast<int>(a0 + 0.5f)));
            d[1] = static_cast<uint8_t>(juce::jlimit(0, 255, static_cast<int>(a1 + 0.5f)));
            d[2] = static_cast<uint8_t>(juce::jlimit(0, 255, static_cast<int>(a2 + 0.5f)));
            d[3] = static_cast<uint8_t>(juce::jlimit(0, 255, static_cast<int>(a3 + 0.5f)));
        }
    }
    return out;
}

void inkify(juce::Image& img, juce::Colour ink) {
    if (!img.isValid()) return;
    img = img.convertedToFormat(juce::Image::ARGB);
    juce::Image::BitmapData d(img, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < d.height; ++y) {
        auto* p = reinterpret_cast<juce::PixelARGB*>(d.getLinePointer(y));
        for (int x = 0; x < d.width; ++x) {
            juce::PixelARGB px = p[x];
            if (px.getAlpha() == 0) continue;
            px.unpremultiply();
            const float v = (px.getRed() + px.getGreen() + px.getBlue()) / (3.0f * 255.0f);
            const auto a = static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f, px.getAlpha() * juce::jmin(1.0f, v * 1.25f)));
            px.setARGB(a, ink.getRed(), ink.getGreen(), ink.getBlue());
            px.premultiply();
            p[x] = px;
        }
    }
}

void Sprites::inkify(int firstId, int lastId, juce::Colour ink) {
    for (int i = firstId; i <= lastId && i < static_cast<int>(imgs_.size()); ++i) assets::inkify(imgs_[static_cast<size_t>(i)], ink);
}

void Sprites::build(double pxPerDesign) {
    scale_ = pxPerDesign;
    imgs_.assign(static_cast<size_t>(atlas::kCount), juce::Image());
    const auto& a = raw().atlas;
    for (int i = 0; i < atlas::kCount; ++i) {
        const auto& s = atlas::table[i];
        const int dw = juce::jmax(1, juce::roundToInt(s.w * pxPerDesign / 2.0));
        const int dh = juce::jmax(1, juce::roundToInt(s.h * pxPerDesign / 2.0));
        imgs_[static_cast<size_t>(i)] = resample(a, {s.x, s.y, s.w, s.h}, dw, dh);
    }
}

} // namespace assets
