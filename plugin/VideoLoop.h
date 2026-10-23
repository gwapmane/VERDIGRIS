// VERDIGRIS - your own video in the coil window. A clip is decoded ONCE, when you load it, into a short run of small
// tall pictures (centre-cropped to the window), so playing it costs almost nothing. Long clips keep their first 12 seconds.
//   * animated GIF: everywhere
//   * MP4 / MOV / WMV / AVI / M4V: on Windows, through the system's Media Foundation
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// MP4 / MOV / WMV / AVI decoding through Windows Media Foundation (Windows only; CMake can switch it off).
#if JUCE_WINDOWS && !defined(VERDIGRIS_NO_MF)
#define VERDIGRIS_HAVE_MF 1
#else
#define VERDIGRIS_HAVE_MF 0
#endif

#include <functional>
#include <memory>
#include <vector>

struct VideoClip {
    static constexpr int kW = 120, kH = 220; // frames are tall, like the window (216 : 396)
    static constexpr double kMaxSeconds = 12.0;
    static constexpr int kMaxFrames = 360;
    std::vector<juce::Image> frames;        // ARGB, opaque
    std::vector<double> endTimes;           // cumulative end time of each frame (seconds)
    double duration = 0.0;

    // the frame to show at time t (the clip loops)
    int frameAt(double t) const;
};
using VideoClipPtr = std::shared_ptr<const VideoClip>;

// Helpers shared by the decoders.
namespace videoutil {
// Centre-crops a picture to the window shape and resizes it to kW x kH. 'rgba' is w*h*4 bytes in R,G,B,A order.
juce::Image squareFrame(const uint8_t* rgba, int w, int h, int strideBytes, bool bgra);
} // namespace videoutil

VideoClipPtr decodeGif(const void* data, size_t size, juce::String& error);
#if VERDIGRIS_HAVE_MF
VideoClipPtr decodeWithMediaFoundation(const juce::File& file, juce::String& error);
#endif

// Loads a clip on a background thread; the callback runs on the message thread.
class VideoLoader : private juce::Thread {
public:
    VideoLoader() : juce::Thread("VERDIGRIS video loader") {}
    ~VideoLoader() override;

    using Done = std::function<void(VideoClipPtr clip, juce::String error)>;
    void load(const juce::File& file, Done done);
    void cancel();

private:
    void run() override;

    juce::CriticalSection lock_;
    juce::File file_;
    Done done_;
    int generation_ = 0;
};
