#include "Body.h"

#include "Assets.h"

namespace body {
namespace {

juce::Colour mixWith(juce::Colour body, float amount, juce::uint8 grey) {
    auto m = [&](float c) { return static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f, c * amount + grey * (1.0f - amount) + 0.5f)); };
    return juce::Colour(m(body.getRed()), m(body.getGreen()), m(body.getBlue()));
}

// The picture, scaled to cover the whole body and cropped in the middle.
juce::Image loadCoverUncached(const juce::File& f, int W, int H) {
    auto src = juce::ImageFileFormat::loadFrom(f);
    if (!src.isValid()) return {};
    src = src.convertedToFormat(juce::Image::ARGB);
    const double k = juce::jmax(static_cast<double>(W) / src.getWidth(), static_cast<double>(H) / src.getHeight());
    const int tw = juce::jmax(W, static_cast<int>(std::ceil(src.getWidth() * k)));
    const int th = juce::jmax(H, static_cast<int>(std::ceil(src.getHeight() * k)));
    auto big = assets::resample(src, src.getBounds(), tw, th);
    return big.getClippedImage({(tw - W) / 2, (th - H) / 2, W, H}).createCopy();
}

// The result is remembered, so changing the colour or texture while a picture is in use does not decode and resize
// it again each time (message thread only).
juce::Image loadCover(const juce::File& f, int W, int H) {
    static juce::Image cached;
    static juce::String cachedPath;
    static juce::Time cachedTime;
    static int cachedW = 0, cachedH = 0;
    const auto stamp = f.getLastModificationTime();
    if (cached.isValid() && cachedPath == f.getFullPathName() && cachedTime == stamp && cachedW == W && cachedH == H) return cached;
    cached = loadCoverUncached(f, W, H);
    cachedPath = f.getFullPathName();
    cachedTime = stamp;
    cachedW = W;
    cachedH = H;
    return cached;
}

inline float overlayBlend(float c, float t) { return c < 0.5f ? 2.0f * c * t : 1.0f - 2.0f * (1.0f - c) * (1.0f - t); }

} // namespace

juce::Colour seatA(juce::Colour b) { return mixWith(b, 0.18f, 0x14); }
juce::Colour seatB(juce::Colour b) { return mixWith(b, 0.24f, 0x21); }
juce::Colour seatC(juce::Colour b) { return mixWith(b, 0.31f, 0x33); }

juce::Image build(const LookSettings& look, const juce::File& photoFile) {
    const auto& R = assets::raw();
    const int W = R.body[0].getWidth(), H = R.body[0].getHeight();
    juce::Image out(juce::Image::ARGB, W, H, false);

    juce::Image photo;
    if (look.photoFile.isNotEmpty() && photoFile.existsAsFile()) photo = loadCover(photoFile, W, H);
    const bool usePhoto = photo.isValid() && photo.getWidth() == W && photo.getHeight() == H;

    const int texId = look.texture - 1; // 0..3, or -1 for plain
    const bool useTex = texId >= 0 && texId < 4 && look.textureStrength > 0.001f;
    const float strength = look.textureStrength;
    const float op = look.photoOpacity;

    juce::Image::BitmapData d0(R.body[0], juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData d5(R.body[1], juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData d1(R.body[2], juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dout(out, juce::Image::BitmapData::writeOnly);
    std::unique_ptr<juce::Image::BitmapData> dtex, dpho;
    if (useTex) dtex = std::make_unique<juce::Image::BitmapData>(assets::texture(texId), juce::Image::BitmapData::readOnly);
    if (usePhoto) dpho = std::make_unique<juce::Image::BitmapData>(photo, juce::Image::BitmapData::readOnly);

    const float bodyC[3] = {look.body.getFloatRed(), look.body.getFloatGreen(), look.body.getFloatBlue()};
    constexpr float k255 = 1.0f / 255.0f;

    for (int y = 0; y < H; ++y) {
        auto* l0 = reinterpret_cast<const juce::PixelARGB*>(d0.getLinePointer(y));
        auto* l5 = reinterpret_cast<const juce::PixelARGB*>(d5.getLinePointer(y));
        auto* l1 = reinterpret_cast<const juce::PixelARGB*>(d1.getLinePointer(y));
        auto* lo = reinterpret_cast<juce::PixelARGB*>(dout.getLinePointer(y));
        const juce::PixelARGB* lt = useTex ? reinterpret_cast<const juce::PixelARGB*>(dtex->getLinePointer(y)) : nullptr;
        const juce::PixelARGB* lp = usePhoto ? reinterpret_cast<const juce::PixelARGB*>(dpho->getLinePointer(y)) : nullptr;
        for (int x = 0; x < W; ++x) {
            const float r0[3] = {l0[x].getRed() * k255, l0[x].getGreen() * k255, l0[x].getBlue() * k255};
            const float r5[3] = {l5[x].getRed() * k255, l5[x].getGreen() * k255, l5[x].getBlue() * k255};
            const float r1[3] = {l1[x].getRed() * k255, l1[x].getGreen() * k255, l1[x].getBlue() * k255};
            float base[3] = {bodyC[0], bodyC[1], bodyC[2]};
            if (lp != nullptr) {
                base[0] += (lp[x].getRed() * k255 - base[0]) * op;
                base[1] += (lp[x].getGreen() * k255 - base[1]) * op;
                base[2] += (lp[x].getBlue() * k255 - base[2]) * op;
            }
            float c[3];
            for (int i = 0; i < 3; ++i) {
                const float b = base[i];
                c[i] = b < 0.5f ? r0[i] + (r5[i] - r0[i]) * (b * 2.0f) : r5[i] + (r1[i] - r5[i]) * ((b - 0.5f) * 2.0f);
            }
            if (lt != nullptr) {
                const float t = lt[x].getRed() * k255;
                for (int i = 0; i < 3; ++i) c[i] += (overlayBlend(juce::jlimit(0.0f, 1.0f, c[i]), t) - c[i]) * strength;
            }
            lo[x].setARGB(255, static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>(c[0] * 255.0f + 0.5f))),
                          static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>(c[1] * 255.0f + 0.5f))),
                          static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>(c[2] * 255.0f + 0.5f))));
        }
    }
    return out;
}

} // namespace body
