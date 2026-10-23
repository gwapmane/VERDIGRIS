#include "VideoLoop.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Assets.h"

int VideoClip::frameAt(double t) const {
    if (frames.empty() || duration <= 0.0) return 0;
    double m = std::fmod(t, duration);
    if (m < 0) m += duration;
    const auto it = std::upper_bound(endTimes.begin(), endTimes.end(), m);
    return juce::jlimit(0, static_cast<int>(frames.size()) - 1, static_cast<int>(it - endTimes.begin()));
}

namespace videoutil {
juce::Image squareFrame(const uint8_t* px, int w, int h, int stride, bool bgra) {
    // centre-crop to the window's tall shape (kW : kH), build an opaque ARGB copy, then shrink it
    const double aspect = static_cast<double>(VideoClip::kW) / VideoClip::kH;
    int cw = w, ch = h;
    if (static_cast<double>(w) / h > aspect) cw = juce::jmax(1, static_cast<int>(std::lround(h * aspect)));
    else ch = juce::jmax(1, static_cast<int>(std::lround(w / aspect)));
    const int x0 = (w - cw) / 2, y0 = (h - ch) / 2;
    juce::Image full(juce::Image::ARGB, cw, ch, false);
    {
        juce::Image::BitmapData bd(full, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < ch; ++y) {
            auto* dst = reinterpret_cast<juce::PixelARGB*>(bd.getLinePointer(y));
            const uint8_t* src = px + static_cast<size_t>(y0 + y) * static_cast<size_t>(stride) + static_cast<size_t>(x0) * 4u;
            for (int x = 0; x < cw; ++x, src += 4) {
                const uint8_t r = bgra ? src[2] : src[0];
                const uint8_t g = src[1];
                const uint8_t b = bgra ? src[0] : src[2];
                dst[x].setARGB(255, r, g, b);
            }
        }
    }
    if (cw == VideoClip::kW && ch == VideoClip::kH) return full;
    return assets::resample(full, full.getBounds(), VideoClip::kW, VideoClip::kH);
}
} // namespace videoutil

// ============================================================================================ GIF decoder
namespace {

struct Reader {
    const uint8_t* p;
    size_t n, i = 0;
    bool ok = true;
    int u8() { if (i >= n) { ok = false; return 0; } return p[i++]; }
    int u16() { const int a = u8(); const int b = u8(); return a | (b << 8); }
    void skip(size_t k) { if (i + k > n) { ok = false; i = n; } else i += k; }
    void skipSubBlocks() { for (;;) { const int len = u8(); if (!ok || len == 0) return; skip(static_cast<size_t>(len)); } }
};

// Variable-width LZW as used by GIF. Returns false on corrupt data.
bool lzwDecode(const std::vector<uint8_t>& data, int minCode, std::vector<uint8_t>& out, size_t wanted) {
    if (minCode < 2 || minCode > 11) return false;
    const int clear = 1 << minCode, eoi = clear + 1;
    std::vector<int16_t> prefix(4096, -1);
    std::vector<uint8_t> suffix(4096, 0), stack(4097);
    for (int i = 0; i < clear; ++i) suffix[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    int codeSize = minCode + 1, next = eoi + 1, prev = -1;
    uint32_t bits = 0;
    int nbits = 0;
    size_t pos = 0;
    out.clear();
    out.reserve(wanted);
    uint8_t first = 0;
    while (out.size() < wanted) {
        while (nbits < codeSize) {
            if (pos >= data.size()) return !out.empty();
            bits |= static_cast<uint32_t>(data[pos++]) << nbits;
            nbits += 8;
        }
        int code = static_cast<int>(bits & ((1u << codeSize) - 1u));
        bits >>= codeSize;
        nbits -= codeSize;
        if (code == clear) { codeSize = minCode + 1; next = eoi + 1; prev = -1; continue; }
        if (code == eoi) break;
        int cur = code;
        size_t sp = 0;
        if (prev < 0) {
            if (code >= clear) return false;
            out.push_back(static_cast<uint8_t>(code));
            first = static_cast<uint8_t>(code);
            prev = code;
            continue;
        }
        if (code >= next) { // the "KwKwK" case
            if (code > next) return false;
            stack[sp++] = first;
            cur = prev;
        }
        while (cur >= clear) {
            if (sp >= 4096) return false;
            stack[sp++] = suffix[static_cast<size_t>(cur)];
            cur = prefix[static_cast<size_t>(cur)];
            if (cur < 0) return false;
        }
        first = suffix[static_cast<size_t>(cur)];
        stack[sp++] = first;
        while (sp > 0) out.push_back(stack[--sp]);
        if (next < 4096) {
            prefix[static_cast<size_t>(next)] = static_cast<int16_t>(prev);
            suffix[static_cast<size_t>(next)] = first;
            ++next;
            if (next == (1 << codeSize) && codeSize < 12) ++codeSize;
        }
        prev = code;
    }
    return true;
}

} // namespace

VideoClipPtr decodeGif(const void* data, size_t size, juce::String& error) {
    Reader r{static_cast<const uint8_t*>(data), size};
    if (size < 13 || std::memcmp(r.p, "GIF8", 4) != 0) { error = "That does not look like a GIF."; return nullptr; }
    r.i = 6;
    const int W = r.u16(), H = r.u16();
    const int lsd = r.u8();
    r.u8(); // background index
    r.u8(); // aspect
    if (!r.ok || W <= 0 || H <= 0 || W > 8192 || H > 8192) { error = "This GIF has an unusual size."; return nullptr; }
    std::vector<uint8_t> globalTable;
    if (lsd & 0x80) {
        const size_t len = 3u << ((lsd & 7) + 1);
        globalTable.assign(r.p + r.i, r.p + juce::jmin(r.n, r.i + len));
        r.skip(len);
    }

    auto clip = std::make_shared<VideoClip>();
    std::vector<uint8_t> canvas(static_cast<size_t>(W) * static_cast<size_t>(H) * 4u, 0); // RGBA
    std::vector<uint8_t> backup;
    int gcDelay = 10, gcDisposal = 0, gcTransparent = -1;
    int prevDisposal = 0, prevX = 0, prevY = 0, prevW = 0, prevH = 0;
    double t = 0.0;

    while (r.ok && r.i < r.n) {
        const int block = r.u8();
        if (block == 0x3B) break;
        if (block == 0x21) {
            const int label = r.u8();
            if (label == 0xF9) {
                const int len = r.u8();
                if (len >= 4) {
                    const int pk = r.u8();
                    gcDisposal = (pk >> 2) & 7;
                    gcDelay = r.u16();
                    const int ti = r.u8();
                    gcTransparent = (pk & 1) ? ti : -1;
                    r.skip(static_cast<size_t>(len - 4));
                } else r.skip(static_cast<size_t>(len));
                r.skipSubBlocks();
            } else r.skipSubBlocks();
            continue;
        }
        if (block != 0x2C) continue;

        const int fx = r.u16(), fy = r.u16(), fw = r.u16(), fh = r.u16();
        const int pk = r.u8();
        std::vector<uint8_t> table = globalTable;
        if (pk & 0x80) {
            const size_t len = 3u << ((pk & 7) + 1);
            table.assign(r.p + r.i, r.p + juce::jmin(r.n, r.i + len));
            r.skip(len);
        }
        const int minCode = r.u8();
        std::vector<uint8_t> packed;
        for (;;) {
            const int len = r.u8();
            if (!r.ok || len == 0) break;
            if (r.i + static_cast<size_t>(len) > r.n) { r.ok = false; break; }
            packed.insert(packed.end(), r.p + r.i, r.p + r.i + len);
            r.i += static_cast<size_t>(len);
        }
        if (!r.ok || fw <= 0 || fh <= 0) break;

        std::vector<uint8_t> idx;
        if (!lzwDecode(packed, minCode, idx, static_cast<size_t>(fw) * static_cast<size_t>(fh))) { error = "This GIF is damaged."; if (clip->frames.empty()) return nullptr; break; }
        idx.resize(static_cast<size_t>(fw) * static_cast<size_t>(fh), 0);

        // undo the previous frame, as its disposal method asks
        if (prevDisposal == 2) {
            for (int y = prevY; y < juce::jmin(H, prevY + prevH); ++y)
                for (int x = prevX; x < juce::jmin(W, prevX + prevW); ++x)
                    std::memset(&canvas[(static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4u], 0, 4);
        } else if (prevDisposal == 3 && !backup.empty()) {
            canvas = backup;
        }
        backup.clear();
        if (gcDisposal == 3) backup = canvas;

        // row order (interlaced GIFs store rows 0,8,16.. then 4,12.. then 2,6.. then 1,3,5..)
        std::vector<int> rowOf(static_cast<size_t>(fh));
        if (pk & 0x40) {
            int row = 0;
            static const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
            for (int pass = 0; pass < 4; ++pass)
                for (int y = start[pass]; y < fh; y += step[pass]) rowOf[static_cast<size_t>(row++)] = y;
        } else {
            for (int y = 0; y < fh; ++y) rowOf[static_cast<size_t>(y)] = y;
        }
        for (int sy = 0; sy < fh; ++sy) {
            const int y = fy + rowOf[static_cast<size_t>(sy)];
            if (y < 0 || y >= H) continue;
            for (int sx = 0; sx < fw; ++sx) {
                const int x = fx + sx;
                if (x < 0 || x >= W) continue;
                const int c = idx[static_cast<size_t>(sy) * static_cast<size_t>(fw) + static_cast<size_t>(sx)];
                if (c == gcTransparent) continue;
                if (static_cast<size_t>(c) * 3u + 2u >= table.size()) continue;
                uint8_t* d = &canvas[(static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4u];
                d[0] = table[static_cast<size_t>(c) * 3u];
                d[1] = table[static_cast<size_t>(c) * 3u + 1u];
                d[2] = table[static_cast<size_t>(c) * 3u + 2u];
                d[3] = 255;
            }
        }
        prevDisposal = gcDisposal;
        prevX = fx; prevY = fy; prevW = fw; prevH = fh;

        // transparent pixels show as black
        std::vector<uint8_t> flat(canvas);
        for (size_t i = 0; i < flat.size(); i += 4)
            if (flat[i + 3] == 0) { flat[i] = flat[i + 1] = flat[i + 2] = 0; }
        clip->frames.push_back(videoutil::squareFrame(flat.data(), W, H, W * 4, false));
        const double d = (gcDelay < 2 ? 10 : gcDelay) / 100.0;
        t += d;
        clip->endTimes.push_back(t);
        gcDelay = 10; gcTransparent = -1; gcDisposal = 0;
        if (t >= VideoClip::kMaxSeconds || static_cast<int>(clip->frames.size()) >= VideoClip::kMaxFrames) break;
    }
    if (clip->frames.empty()) { if (error.isEmpty()) error = "No pictures found in this GIF."; return nullptr; }
    clip->duration = t;
    return clip;
}

// ============================================================================================ loader thread
VideoLoader::~VideoLoader() { stopThread(4000); }

void VideoLoader::cancel() {
    const juce::ScopedLock sl(lock_);
    ++generation_;
    done_ = nullptr;
}

void VideoLoader::load(const juce::File& file, Done done) {
    {
        const juce::ScopedLock sl(lock_);
        ++generation_;
        file_ = file;
        done_ = std::move(done);
    }
    if (!isThreadRunning()) startThread(juce::Thread::Priority::low);
    else notify();
}

void VideoLoader::run() {
    for (;;) {
        juce::File file;
        Done done;
        int gen;
        {
            const juce::ScopedLock sl(lock_);
            file = file_;
            done = done_;
            gen = generation_;
        }
        if (!done) return;

        VideoClipPtr clip;
        juce::String error;
        const auto ext = file.getFileExtension().toLowerCase();
        if (!file.existsAsFile()) {
            error = "That file could not be found.";
        } else if (ext == ".gif") {
            juce::MemoryBlock mb;
            if (file.loadFileAsData(mb)) clip = decodeGif(mb.getData(), mb.getSize(), error);
            else error = "That file could not be read.";
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            // a still picture: a clip of one frame
            auto img = juce::ImageFileFormat::loadFrom(file);
            if (img.isValid()) {
                img = img.convertedToFormat(juce::Image::ARGB);
                juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
                auto c = std::make_shared<VideoClip>();
                c->frames.push_back(videoutil::squareFrame(bd.data, img.getWidth(), img.getHeight(), bd.lineStride, true));
                c->endTimes.push_back(1.0);
                c->duration = 1.0;
                clip = c;
            } else {
                error = "That picture could not be opened. Try a JPG or PNG.";
            }
        } else {
#if VERDIGRIS_HAVE_MF
            clip = decodeWithMediaFoundation(file, error);
#else
            error = "Video files other than GIF are supported on Windows. Please use a GIF here.";
#endif
        }
        if (clip == nullptr && error.isEmpty()) error = "That video could not be read. Try a GIF, or an MP4 with standard H.264 video.";

        bool current;
        {
            const juce::ScopedLock sl(lock_);
            current = (gen == generation_);
            if (current) done_ = nullptr;
        }
        if (current)
            juce::MessageManager::callAsync([done, clip, error] { done(clip, error); });
        if (current || threadShouldExit()) return;
    }
}
