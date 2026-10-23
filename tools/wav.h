// tiny WAV reader/writer for the test tools (16-bit PCM or 32-bit float in, 32-bit float out)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static inline void rd(void* p, size_t sz, size_t n, FILE* f) { if (std::fread(p, sz, n, f) != n) { /* short read: ignore */ } }
struct Wav { int sr = 44100, ch = 1; std::vector<float> d; }; // interleaved
inline bool readWav(const std::string& path, Wav& w) {
    FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return false;
    char id[4]; uint32_t sz; rd(id, 1, 4, f); rd(&sz, 4, 1, f); rd(id, 1, 4, f);
    int fmt = 1, bits = 16;
    while (std::fread(id, 1, 4, f) == 4 && std::fread(&sz, 4, 1, f) == 1) {
        if (!std::memcmp(id, "fmt ", 4)) {
            uint16_t a, c, b; uint32_t sr, br; uint16_t ba;
            rd(&a, 2, 1, f); rd(&c, 2, 1, f); rd(&sr, 4, 1, f); rd(&br, 4, 1, f); rd(&ba, 2, 1, f); rd(&b, 2, 1, f);
            fmt = a; w.ch = c; w.sr = static_cast<int>(sr); bits = b; if (sz > 16) std::fseek(f, sz - 16, SEEK_CUR);
        } else if (!std::memcmp(id, "data", 4)) {
            const size_t n = sz / (bits / 8); w.d.resize(n);
            if (bits == 16) { std::vector<int16_t> t(n); rd(t.data(), 2, n, f); for (size_t i = 0; i < n; ++i) w.d[i] = t[i] / 32768.0f; }
            else if (bits == 32 && fmt == 3) rd(w.d.data(), 4, n, f);
            else { std::fclose(f); return false; }
            std::fclose(f); return true;
        } else std::fseek(f, sz, SEEK_CUR);
    }
    std::fclose(f); return false;
}
inline bool writeWav(const std::string& path, const Wav& w) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    const uint32_t n = static_cast<uint32_t>(w.d.size()), dsz = n * 4, riff = 36 + dsz, fmtsz = 16, srr = static_cast<uint32_t>(w.sr), br = srr * w.ch * 4;
    const uint16_t fmt = 3, ch = static_cast<uint16_t>(w.ch), ba = static_cast<uint16_t>(w.ch * 4), bits = 32;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&fmtsz, 4, 1, f);
    std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&srr, 4, 1, f); std::fwrite(&br, 4, 1, f); std::fwrite(&ba, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&dsz, 4, 1, f); std::fwrite(w.d.data(), 4, n, f); std::fclose(f); return true;
}
