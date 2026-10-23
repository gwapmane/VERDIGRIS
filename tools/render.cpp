// render: run a WAV through the fuzz engine. usage: render in.wav out.wav [key=value ...]
// keys: every Params field (fuzz bias volume type voicing on octave starve chop crush wash swell octAmt starveAmt sag chopDepth
//       chopSmooth crushAmt washMix washDecay swellTime chopPattern chopStep tone gate mix hfe temp drift spread pickup guitar
//       lowcut mids supply rampRate rampDepth rampShape rampSync rampTargets inputGainDb outputGainDb) plus bpm, block, tail
#include "../core/fuzz.h"
#include "wav.h"
#include <cstdlib>
#include <map>
#include <string>
using namespace verdigris;
bool setParam(Params& p, const std::string& k, double v) {
    const float f = static_cast<float>(v); const int i = static_cast<int>(v);
#define F(n) if (k == #n) { p.n = f; return true; }
#define I(n) if (k == #n) { p.n = i; return true; }
#define B(n) if (k == #n) { p.n = v != 0; return true; }
    F(fuzz) F(bias) F(volume) I(type) I(voicing) B(on) B(octave) B(starve) B(chop) B(crush) B(wash) B(swell) F(octAmt) F(starveAmt) F(sag)
    F(chopDepth) F(chopSmooth) F(crushAmt) F(washMix) F(washDecay) F(swellTime) I(chopPattern) I(chopStep) F(tone) F(gate) F(mix) F(hfe)
    F(temp) F(drift) F(spread) F(pickup) F(guitar) F(lowcut) F(mids) F(supply) F(rampRate) F(rampDepth) I(rampShape) I(rampSync)
    F(inputGainDb) F(outputGainDb)
    if (k == "rampTargets") { p.rampTargets = static_cast<uint32_t>(i); return true; }
    return false;
}
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: render in.wav out.wav [key=value ...]\n"); return 1; }
    Wav in; if (!readWav(argv[1], in)) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    Params p; Transport t; t.hostValid = false; t.bpm = 120; int block = 512; double tail = 1.5; double sr = in.sr; double gainIn = 1.0;
    for (int a = 3; a < argc; ++a) {
        std::string s = argv[a]; const size_t e = s.find('='); if (e == std::string::npos) continue;
        const std::string k = s.substr(0, e); const double v = std::atof(s.c_str() + e + 1);
        if (k == "bpm") t.bpm = v; else if (k == "block") block = static_cast<int>(v); else if (k == "tail") tail = v; else if (k == "sr") sr = v; else if (k == "in") gainIn = v;
        else if (!setParam(p, k, v)) { std::fprintf(stderr, "unknown key %s\n", k.c_str()); return 1; }
    }
    Engine e; e.setParams(p); e.setTransport(t); e.prepare(sr);
    const int ch = in.ch; const size_t nIn = in.d.size() / ch, nOut = nIn + static_cast<size_t>(tail * sr);
    Wav out; out.sr = static_cast<int>(sr); out.ch = 2; out.d.assign(nOut * 2, 0.0f);
    std::vector<float> l(block), r(block), ol(block), orr(block);
    for (size_t pos = 0; pos < nOut; pos += block) {
        const int n = static_cast<int>(std::min<size_t>(block, nOut - pos));
        for (int i = 0; i < n; ++i) { const size_t k = pos + i; const float v = k < nIn ? static_cast<float>(in.d[k * ch] * gainIn) : 0.0f; l[i] = r[i] = v; }
        e.process(l.data(), r.data(), ol.data(), orr.data(), n);
        for (int i = 0; i < n; ++i) { out.d[(pos + i) * 2] = ol[i]; out.d[(pos + i) * 2 + 1] = orr[i]; }
    }
    writeWav(argv[2], out);
    double pk = 0, ss = 0; for (size_t i = 0; i < out.d.size(); i += 2) { pk = std::max<double>(pk, std::fabs(out.d[i])); ss += out.d[i] * out.d[i]; }
    std::printf("peak %.1f dBFS  rms %.1f dBFS  latency %d  solver fails %lld\n", 20 * std::log10(pk + 1e-12), 10 * std::log10(ss / nOut + 1e-18), e.latencySamples(), e.solverFailures());
    return 0;
}
