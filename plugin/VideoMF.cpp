// VERDIGRIS - decoding MP4 / MOV / WMV / AVI with Windows' own Media Foundation (no extra files needed).
// Windows only; on other systems this file compiles to nothing.
#include <juce_core/juce_core.h>

#include "VideoLoop.h"

#if VERDIGRIS_HAVE_MF

#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <vector>

#include "VideoLoop.h"

namespace {

template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() { if (p != nullptr) { p->Release(); p = nullptr; } }
    T** put() { reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct MfSession {
    bool comOk = false, mfOk = false;
    MfSession() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comOk = SUCCEEDED(hr); // S_FALSE (already initialised) is fine and still needs the matching uninitialise
        mfOk = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~MfSession() {
        if (mfOk) MFShutdown();
        if (comOk) CoUninitialize();
    }
};

} // namespace

VideoClipPtr decodeWithMediaFoundation(const juce::File& file, juce::String& error) {
    MfSession session;
    if (!session.mfOk) { error = "Windows could not start its video decoder."; return nullptr; }

    Com<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(attrs.put(), 1))) { error = "Windows could not start its video decoder."; return nullptr; }
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); // lets the reader convert to plain RGB for us

    Com<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(file.getFullPathName().toWideCharPointer(), attrs.p, reader.put()))) {
        error = "Windows could not open that video. MP4 (H.264), MOV, WMV and AVI usually work, and GIF always does.";
        return nullptr;
    }
    const DWORD videoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(videoStream, TRUE))) { error = "That file has no video in it."; return nullptr; }

    Com<IMFMediaType> want;
    MFCreateMediaType(want.put());
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType(videoStream, nullptr, want.p))) {
        error = "Windows could not decode that kind of video. Try an MP4 with H.264, or a GIF.";
        return nullptr;
    }

    UINT32 w = 0, h = 0;
    LONG stride = 0;
    {
        Com<IMFMediaType> got;
        if (FAILED(reader->GetCurrentMediaType(videoStream, got.put()))) { error = "Could not read the video's size."; return nullptr; }
        if (FAILED(MFGetAttributeSize(got.p, MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0) { error = "Could not read the video's size."; return nullptr; }
        UINT32 s = 0;
        if (SUCCEEDED(got->GetUINT32(MF_MT_DEFAULT_STRIDE, &s))) stride = static_cast<LONG>(static_cast<INT32>(s));
        else stride = -static_cast<LONG>(w) * 4; // RGB32 from the reader is bottom-up unless it says otherwise
    }
    const bool bottomUp = stride < 0;
    const size_t rowBytes = static_cast<size_t>(stride < 0 ? -stride : stride);

    auto clip = std::make_shared<VideoClip>();
    constexpr LONGLONG kUnit = 10000000; // 100 ns ticks per second
    const LONGLONG step = kUnit / 24;    // keep about 24 pictures per second
    LONGLONG nextKeep = 0, lastKept = -1;
    std::vector<uint8_t> upright(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);

    for (int guard = 0; guard < 20000; ++guard) {
        DWORD actual = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> sample;
        if (FAILED(reader->ReadSample(videoStream, 0, &actual, &flags, &ts, sample.put()))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (flags & MF_SOURCE_READERF_ERROR) break;
        if (!sample) continue;
        if (ts < nextKeep) continue;
        if (ts > static_cast<LONGLONG>(VideoClip::kMaxSeconds * kUnit)) break;

        Com<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(buf.put()))) continue;
        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buf->Lock(&data, &maxLen, &curLen)) || data == nullptr) continue;
        const bool enough = curLen >= rowBytes * static_cast<size_t>(h);
        if (enough) {
            for (UINT32 y = 0; y < h; ++y) {
                const UINT32 srcRow = bottomUp ? (h - 1 - y) : y;
                std::copy(data + static_cast<size_t>(srcRow) * rowBytes, data + static_cast<size_t>(srcRow) * rowBytes + static_cast<size_t>(w) * 4u,
                          upright.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * static_cast<size_t>(w) * 4u));
            }
        }
        buf->Unlock();
        if (!enough) continue;

        clip->frames.push_back(videoutil::squareFrame(upright.data(), static_cast<int>(w), static_cast<int>(h), static_cast<int>(w) * 4, true));
        // a frame is shown until the next kept one begins
        if (lastKept >= 0) clip->endTimes.push_back(static_cast<double>(ts) / kUnit);
        else clip->endTimes.push_back(0.0);
        lastKept = ts;
        nextKeep = ts + step * 9 / 10;
        if (static_cast<int>(clip->frames.size()) >= VideoClip::kMaxFrames) break;
    }

    if (clip->frames.empty()) { error = "No pictures could be read from that video."; return nullptr; }
    // endTimes[i] holds the START time of frame i so far; turn them into end times
    const size_t n = clip->frames.size();
    std::vector<double> starts = clip->endTimes;
    double lastLen = n > 1 ? (starts[n - 1] - starts[n - 2]) : 1.0 / 24.0;
    if (lastLen <= 0.0) lastLen = 1.0 / 24.0;
    for (size_t i = 0; i + 1 < n; ++i) clip->endTimes[i] = starts[i + 1] - starts[0];
    clip->endTimes[n - 1] = (starts[n - 1] - starts[0]) + lastLen;
    clip->duration = clip->endTimes[n - 1];
    return clip;
}

#endif // VERDIGRIS_HAVE_MF
