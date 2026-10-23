// VERDIGRIS - the fuzz engine. Plain C++ (no JUCE), so it can be tested on its own.
//
//   guitar -> trim -> low cut -> [OCTAVE] -> pad -> 4x oversampled transistor circuit (circuit.h) -> tone / mids / gate
//          -> [CRUSH] -> [CHOP] -> [SWELL] -> [WASH send] -> volume -> mix with the dry guitar
//
// The six side switches are the "textures": OCTAVE, STARVE, CHOP, CRUSH, WASH and SWELL. Each one fades in and out
// over a few milliseconds, so they can be flipped as fast as you like without clicks.
#pragma once
#include "circuit.h"
#include "tempo.h"
#include "levelcal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace verdigris {

constexpr double kPi = 3.14159265358979323846;

enum : uint32_t { kRampFuzz = 1u << 0, kRampBias = 1u << 1, kRampTone = 1u << 2, kRampSupply = 1u << 3, kRampMix = 1u << 4 };
enum : int { kTypeGe = 0, kTypeSi = 1 };
enum : int { kFat = 0, kZonk = 1, kVin = 2 };

struct Params {
    // the pedal itself
    float fuzz = 0.6f, bias = 0.5f, volume = 0.5f;
    int type = kTypeGe;
    int voicing = kVin;
    bool on = true;
    // side switches and what they do
    bool octave = false, starve = false, chop = false, crush = false, wash = false, swell = false;
    float octAmt = 0.6f, starveAmt = 0.6f, sag = 0.3f, chopDepth = 1.0f, chopSmooth = 0.2f, crushAmt = 0.5f, washMix = 0.4f,
          washDecay = 0.5f, swellTime = 0.5f;
    int chopPattern = 1, chopStep = 2;
    // under the hood
    float tone = 0.55f, gate = 0.0f, mix = 1.0f, hfe = 0.5f, temp = 0.4f, drift = 0.0f, spread = 0.3f, pickup = 0.5f, guitar = 1.0f,
          lowcut = 0.0f, mids = 0.5f, supply = 0.88f;
    // ramp: a slow hand on the knobs, lockable to the song
    float rampRate = 0.4f, rampDepth = 0.0f;
    int rampShape = 0, rampSync = 0;
    uint32_t rampTargets = 0;
    // trims
    float inputGainDb = 0.0f, outputGainDb = 0.0f;
};

namespace dsp {

inline double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
inline float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    void lowpass(double fs, double f, double q) {
        f = std::min(f, fs * 0.45);
        const double w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak(double fs, double f, double q, double gainDb) {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf(double fs, double f, double gainDb) {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
        const double al = s / 2 * std::sqrt(2.0), t = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
            (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
    double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct OnePole {
    double a = 0.0, z = 0.0;
    void setCutoff(double fs, double f) { a = 1.0 - std::exp(-2.0 * kPi * f / fs); }
    double lp(double x) { z += a * (x - z); return z; }
    double hp(double x) { z += a * (x - z); return x - z; }
};

// a value that glides in a straight line to its next target over one control segment
struct LinRamp {
    float cur = 1.0f, step = 0.0f;
    void set(float target, int n) { step = (target - cur) / static_cast<float>(n); }
    float next() { cur += step; return cur; }
};

inline double besselI0(double x) {
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; if (t < 1e-14 * s) break; }
    return s;
}

// half-band FIR (Kaiser window). Used for both directions of a 2x change of sample rate.
class HalfBand {
public:
    void design(int taps, double beta) {
        n_ = taps;
        h_.assign(n_, 0.0);
        buf_.assign(n_, 0.0);
        pos_ = 0;
        const int m = (n_ - 1) / 2;
        const double i0b = besselI0(beta);
        for (int i = 0; i < n_; ++i) {
            const int k = i - m;
            double v;
            if (k == 0) v = 0.5;
            else if ((k & 1) == 0) v = 0.0;
            else v = std::sin(kPi * k / 2.0) / (kPi * k);
            const double r = static_cast<double>(k) / m;
            h_[i] = v * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        }
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0); pos_ = 0; }
    int delay() const { return (n_ - 1) / 2; }
    // one sample in, two out (the filter runs on the zero-stuffed stream, gain 2)
    void up(double x, double& o0, double& o1) { o0 = 2.0 * push(x); o1 = 2.0 * push(0.0); }
    // two samples in, one out
    double down(double a, double b) { push(a); return push(b); }

private:
    double push(double x) {
        buf_[pos_] = x;
        double acc = 0.0;
        int idx = pos_;
        for (int k = 0; k < n_; ++k) {
            acc += h_[k] * buf_[idx];
            if (--idx < 0) idx = n_ - 1;
        }
        if (++pos_ >= n_) pos_ = 0;
        return acc;
    }
    int n_ = 3, pos_ = 0;
    std::vector<double> h_, buf_;
};

class Oversampler {
public:
    void prepare(int factor) {
        factor_ = factor;
        if (factor == 4) { up1_.design(47, 7.5); dn1_.design(47, 7.5); up2_.design(19, 6.5); dn2_.design(19, 6.5); }
        else if (factor == 2) { up1_.design(19, 6.5); dn1_.design(19, 6.5); }
        reset();
    }
    void reset() { up1_.reset(); dn1_.reset(); up2_.reset(); dn2_.reset(); }
    int factor() const { return factor_; }
    // delay in samples at the original rate (can be fractional)
    double latency() const {
        if (factor_ == 4) return up1_.delay() + up2_.delay() / 2.0;
        if (factor_ == 2) return up1_.delay();
        return 0.0;
    }
    template <class F> double process(double x, F&& f) {
        if (factor_ == 1) return f(x);
        double o[2], r[2];
        up1_.up(x, o[0], o[1]);
        for (int i = 0; i < 2; ++i) {
            if (factor_ == 4) {
                double p0, p1;
                up2_.up(o[i], p0, p1);
                const double q0 = f(p0), q1 = f(p1);
                r[i] = dn2_.down(q0, q1);
            } else {
                r[i] = f(o[i]);
            }
        }
        return dn1_.down(r[0], r[1]);
    }

private:
    int factor_ = 1;
    HalfBand up1_, dn1_, up2_, dn2_;
};

// a small stereo reverb: 8 delay lines mixed through a Hadamard matrix, with a little movement so it does not ring
class Reverb {
public:
    void prepare(double fs) {
        fs_ = fs;
        static const double ms[kN] = {23.4, 29.9, 36.7, 41.3, 47.9, 53.1, 61.7, 71.3};
        for (int i = 0; i < kN; ++i) {
            len_[i] = std::max(16, static_cast<int>(ms[i] * 0.001 * fs));
            buf_[i].assign(len_[i] + 64, 0.0f);
            wp_[i] = 0;
            lp_[i] = 0.0f;
            lfoPh_[i] = 0.17 * i;
            lfoInc_[i] = (0.07 + 0.031 * i) / fs;
        }
        ap_[0].assign(static_cast<int>(0.0053 * fs) + 1, 0.0f);
        ap_[1].assign(static_cast<int>(0.0079 * fs) + 1, 0.0f);
        apPos_[0] = apPos_[1] = 0;
        setDecay(0.5f);
        energy_ = 0.0f;
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& a : ap_) std::fill(a.begin(), a.end(), 0.0f);
        std::fill(std::begin(lp_), std::end(lp_), 0.0f);
        energy_ = 0.0f;
    }
    void setDecay(float d) { // 0..1 -> 0.35 s .. 7 s
        const double t60 = 0.35 * std::pow(20.0, static_cast<double>(d));
        for (int i = 0; i < kN; ++i) g_[i] = static_cast<float>(std::pow(10.0, -3.0 * len_[i] / (fs_ * t60)));
        damp_ = static_cast<float>(1.0 - std::exp(-2.0 * kPi * (7000.0 - 4200.0 * d) / fs_));
    }
    bool active() const { return energy_ > 1e-9f; }
    void process(float in, float& oL, float& oR) {
        // two all-pass diffusers on the input
        float x = in;
        for (int k = 0; k < 2; ++k) {
            auto& b = ap_[k];
            float& z = b[apPos_[k]];
            const float y = -0.6f * x + z;
            z = x + 0.6f * y;
            x = y;
            if (++apPos_[k] >= static_cast<int>(b.size())) apPos_[k] = 0;
        }
        float v[kN];
        for (int i = 0; i < kN; ++i) {
            lfoPh_[i] += lfoInc_[i];
            if (lfoPh_[i] >= 1.0) lfoPh_[i] -= 1.0;
            const double d = len_[i] + 6.0 * std::sin(2.0 * kPi * lfoPh_[i]);
            double rp = wp_[i] - d;
            const int sz = static_cast<int>(buf_[i].size());
            while (rp < 0) rp += sz;
            const int i0 = static_cast<int>(rp);
            const float fr = static_cast<float>(rp - i0);
            const float a = buf_[i][i0 % sz], b = buf_[i][(i0 + 1) % sz];
            v[i] = a + (b - a) * fr;
        }
        float outL = 0, outR = 0;
        for (int i = 0; i < kN; ++i) {
            if (i & 1) outR += (i & 2 ? -1.0f : 1.0f) * v[i]; else outL += (i & 2 ? -1.0f : 1.0f) * v[i];
        }
        // Hadamard mix (8 x 8)
        for (int s = 1; s < kN; s <<= 1)
            for (int i = 0; i < kN; i += 2 * s)
                for (int j = i; j < i + s; ++j) {
                    const float a = v[j], b = v[j + s];
                    v[j] = a + b;
                    v[j + s] = a - b;
                }
        for (int i = 0; i < kN; ++i) {
            float y = v[i] * 0.35355339f * g_[i];
            lp_[i] += damp_ * (y - lp_[i]);
            const float w = lp_[i] + (i == 0 ? x : (i == 3 ? 0.6f * x : 0.0f)) * 0.6f;
            buf_[i][wp_[i]] = w;
            if (++wp_[i] >= static_cast<int>(buf_[i].size())) wp_[i] = 0;
        }
        oL = outL * 0.5f;
        oR = outR * 0.5f;
        energy_ += (std::fabs(oL) + std::fabs(oR) - energy_) * 0.0005f;
    }

private:
    static constexpr int kN = 8;
    double fs_ = 44100.0;
    int len_[kN] = {};
    std::vector<float> buf_[kN];
    int wp_[kN] = {};
    float lp_[kN] = {}, g_[kN] = {};
    float damp_ = 0.5f;
    double lfoPh_[kN] = {}, lfoInc_[kN] = {};
    std::vector<float> ap_[2];
    int apPos_[2] = {};
    float energy_ = 0.0f;
};

} // namespace dsp

class Engine {
public:
    static constexpr int kSeg = 32;
    static constexpr double kFullScaleVolts = 0.25; // what a full-scale input is worth at the guitar jack
    static constexpr double kOutScale = 1.0 / 5.0;  // volts at the second collector that make a full-scale output
    static constexpr double kDriveLo = -0.3, kDriveHi = 35.7; // small-signal gain, in dB from the input to just before the volume
    static constexpr int kGainPoints = 17;
    static constexpr double kGainRefHz = 300.0; // the drive curve is defined at this frequency

    void prepare(double fs) {
        fs_ = fs;
        const int f = fs <= 50500.0 ? 4 : (fs <= 100500.0 ? 2 : 1);
        os_.prepare(f);
        fsOs_ = fs * f;
        aEnvAtk_ = 1.0 - std::exp(-1.0 / (0.0005 * fs));
        aEnvRel_ = 1.0 - std::exp(-1.0 / (0.030 * fs));
        latency_ = static_cast<int>(std::lround(os_.latency()));
        int sz = 1;
        while (sz < latency_ + 8) sz <<= 1;
        dryBuf_.assign(sz, 0.0f);
        dryMask_ = sz - 1;
        reverb_.prepare(fs);
        buildGainTable();
        reset();
    }
    void reset() {
        os_.reset();
        reverb_.reset();
        std::fill(dryBuf_.begin(), dryBuf_.end(), 0.0f);
        dryPos_ = 0;
        beat_ = beatFree_ = 0.0;
        rng_ = 0x9E3779B9u;
        lfoPhase_ = 0.0;
        lfoRandCur_ = lfoRandNext_ = 0.0f;
        untilControl_ = 0;
        lastStep_ = -(1LL << 30);
        stepNow_ = 0;
        randOpen_ = true;
        pulseG_ = pgS_ = 1.0f;
        // start from the settled state of the current settings
        fuzz_ = p_.fuzz; bias_ = p_.bias; tone_ = p_.tone; supplyN_ = p_.supply; mix_ = p_.mix;
        octMix_ = p_.octave ? 1.0f : 0.0f; starveMix_ = p_.starve ? 1.0f : 0.0f; chopMix_ = p_.chop ? 1.0f : 0.0f;
        crushMix_ = p_.crush ? 1.0f : 0.0f; washFeed_ = p_.wash ? 1.0f : 0.0f; swellMix_ = p_.swell ? 1.0f : 0.0f;
        onMix_ = p_.on ? 1.0f : 0.0f;
        vccNow_ = vccTarget(0.0);
        driftT_ = 0.0; sputter_ = 0.0;
        envIn_ = envInSlow_ = envOut_ = 0.0;
        swFast_ = swSlow_ = 0.0; swPhase_ = 1.0; swActive_ = false; swCool_ = 0; swG_ = 1.0;
        gateOpen_ = true; gateGain_ = 1.0f; gateRamp_.cur = 1.0f; chopRamp_.cur = 1.0f; duck_ = 0.0f;
        volRamp_.cur = 0.0f; // the first control block sets the real level; start silent
        lowcutHp_ = dsp::OnePole{}; dcPost_ = dsp::OnePole{};
        for (auto* b : {&toneLp_, &midPk_, &shelf_}) b->reset();
        crushHold_ = 0.0; crushPhase_ = 0.0; crushLp_ = dsp::OnePole{};
        octLp_ = dsp::OnePole{};
        octDc_ = dsp::OnePole{};
        meterIn_ = meterOut_ = clipAct_ = 0.0f;
        lastType_ = -1;
        lastVoicing_ = -1;
        rebuildCircuit(true);
        dcTrack_ = circ_.quiescent();
    }
    void setParams(const Params& p) { p_ = p; }
    void setTransport(const Transport& t) { tr_ = t; }
    int latencySamples() const { return latency_; }
    int oversampling() const { return os_.factor(); }

    // ---- what the screen shows ----
    float inputLevel() const { return meterIn_; }
    float outputLevel() const { return meterOut_; }
    float clipActivity() const { return clipAct_; }   // 0..1: how hard the transistors are being driven into the rails
    float supplyVolts() const { return static_cast<float>(vccNow_); }
    float supplyFraction() const { return static_cast<float>(vccNow_ / 9.6); }
    float swellGain() const { return static_cast<float>(swG_); }
    float chopLevel() const { return pgS_; }
    int chopStepNow() const { return stepNow_; }
    bool gateIsOpen() const { return gateOpen_; }
    double beatPosition() const { return beat_; }
    float rampValue() const { return rampOut_; } // -1..1, what the ramp is doing right now
    long long solverFailures() const { return circ_.failures(); }
    double quiescentVolts() const { return circ_.quiescent(); }

    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        int pos = 0;
        while (pos < n) {
            if (untilControl_ == 0) {
                beat_ = (tr_.hostValid && tr_.playing) ? tr_.ppq + pos * beatsPerSample() : beatFree_;
                control(kSeg);
                beatFree_ = beat_ + kSeg * beatsPerSample();
                untilControl_ = kSeg;
            }
            const int seg = std::min(untilControl_, n - pos);
            for (int i = 0; i < seg; ++i) {
                float l, r;
                tick(0.5f * (inL[pos + i] + inR[pos + i]), l, r);
                outL[pos + i] = l;
                outR[pos + i] = r;
            }
            pos += seg;
            untilControl_ -= seg;
        }
    }

private:
    struct VoiceDef {
        double c1, cb, r3mul, rc2mul, trimDb, rsMul, lowShelfDb, midDb, toneMul;
    };
    static const VoiceDef& voice(int v) {
        //                      c1      cb      r3    rc2   trim  rs    low   mid   tone
        static const VoiceDef t[3] = {
            {10e-6, 47e-6, 1.25, 1.00, 0.0, 0.8, 4.5, -3.0, 0.9},  // FAT: big and woolly
            {1.5e-6, 10e-6, 0.55, 0.80, 3.0, 1.0, -3.5, 4.5, 1.25}, // ZONK: hot, gated and nasal
            {2.2e-6, 20e-6, 1.0, 1.15, -2.0, 1.5, 0.0, 0.0, 0.8}};  // VIN: smooth and rounded
        return t[std::min(2, std::max(0, v))];
    }

    float clamp01(float v) const { return dsp::clamp01(v); }
    float coefFor(double sec) const { return 1.0f - std::exp(static_cast<float>(-1.0 / (sec * fs_))); }
    float segCoef(double sec, int seg) const { return 1.0f - std::exp(static_cast<float>(-static_cast<double>(seg) / (sec * fs_))); }
    double bpm() const { return std::min(400.0, std::max(20.0, tr_.bpm)); }
    double beatsPerSample() const { return bpm() / (60.0 * fs_); }
    double bpb() const { return static_cast<double>(std::max(1, tr_.beatsPerBar)); }
    float rnd() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0xFFFFFF) / static_cast<float>(0x800000) - 1.0f;
    }
    float lfoValue() const {
        const float p = static_cast<float>(lfoPhase_);
        switch (p_.rampShape) {
        case 1: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
        case 2: return 2.0f * p - 1.0f;
        case 3: {
            const float t = p * p * (3.0f - 2.0f * p);
            return lfoRandCur_ + (lfoRandNext_ - lfoRandCur_) * t;
        }
        default: return std::sin(6.2831853f * p);
        }
    }

    static Bjt makeBjt(bool si, double hfe, double tempC) {
        Bjt q;
        if (!si) { // a leaky germanium PNP (AC128 style): high saturation current, and the leakage doubles every ~9 degrees
            q.Is = 5e-8 * std::pow(2.0, (tempC - 25.0) / 8.0);
            q.nVt = 0.0259 * 1.2 * (tempC + 273.15) / 298.15;
            q.bf = 90.0 * hfe;
            q.br = 5.0;
            q.icbo = 1.2e-6 * std::pow(2.0, (tempC - 25.0) / 9.0);
        } else { // silicon (BC108 style): tight, high gain, hardly any leakage
            q.Is = 1.2e-14 * std::pow(2.0, (tempC - 25.0) / 6.0);
            q.nVt = 0.0259 * (tempC + 273.15) / 298.15;
            q.bf = 250.0 * hfe;
            q.br = 8.0;
            q.icbo = 1e-10 * std::pow(2.0, (tempC - 25.0) / 9.0);
        }
        return q;
    }

    double vccTarget(double envNorm) const {
        const double base = 4.5 + 5.1 * std::min(1.0, std::max(0.0, static_cast<double>(supplyN_)));
        double v = base * (1.0 - 0.52 * starveMix_ * p_.starveAmt);
        const double sagAmt = std::max(static_cast<double>(p_.sag) * 0.5, static_cast<double>(starveMix_) * (0.25 + 0.75 * p_.starveAmt));
        v -= base * 0.34 * sagAmt * std::min(1.0, envNorm / 0.6); // 0.6: about what a saturated note measures
        v *= 1.0 + 0.07 * starveMix_ * p_.starveAmt * sputter_;
        return std::max(2.2, v);
    }

    // the circuit for a set of parameters (also used on its own to measure the gain table)
    static CircuitCfg makeCfg(const Params& pp, double fuzz, double bias, double vcc, double driftT, double fsOs) {
        (void)fsOs;
        const VoiceDef& vo = voice(pp.voicing);
        const bool si = pp.type == kTypeSi;
        const double tempC = std::min(43.0, 5.0 + 36.0 * pp.temp + 5.0 * pp.drift * driftT); // hotter than this and a Ge fuzz just dies
        const double hf = std::pow(2.0, (pp.hfe - 0.5) * 2.0);
        Bjt q1 = makeBjt(si, hf * (1.0 - 0.22 * pp.spread), tempC);
        Bjt q2 = makeBjt(si, hf * (1.0 + 0.45 * pp.spread), tempC);
        q2.Is *= 1.0 + 0.5 * pp.spread;
        CircuitCfg c;
        c.vcc = vcc;
        c.r1 = 33e3;
        c.r3 = 100e3 * vo.r3mul * std::pow(3.0, (bias - 0.5) * 2.0);
        c.rc2 = 8670.0 * vo.rc2mul * (si ? 0.68 : 1.0); // silicon runs a lower collector load to sit mid-rail
        c.fuzz = fuzz;
        c.c1 = vo.c1;
        c.cb = vo.cb;
        c.rs = 1000.0 * std::pow(30.0, static_cast<double>(pp.pickup)) * vo.rsMul;
        c.q1 = q1;
        c.q2 = q2;
        return c;
    }

    // small-signal gain (dB) of the standard circuit at kGainPoints positions of the fuzz knob, per type and voicing
    void buildGainTable() {
        for (int t = 0; t < 2; ++t)
            for (int v = 0; v < 3; ++v) {
                Params pp;
                pp.type = t;
                pp.voicing = v;
                for (int i = 0; i < kGainPoints; ++i) {
                    Circuit c;
                    c.setSampleRate(fsOs_);
                    c.configure(makeCfg(pp, static_cast<double>(i) / (kGainPoints - 1), 0.5, 4.5 + 5.1 * 0.88, 0.0, fsOs_));
                    const double g = std::fabs(c.acGain(kGainRefHz));
                    gainTable_[t][v][i] = 20.0 * std::log10(std::max(g, 1e-3));
                }
            }
    }
    double refGainDb(double f) const {
        const double x = std::min(1.0, std::max(0.0, f)) * (kGainPoints - 1);
        const int i = std::min(kGainPoints - 2, static_cast<int>(x));
        const double fr = x - i;
        const auto& g = gainTable_[std::min(1, std::max(0, p_.type))][std::min(2, std::max(0, p_.voicing))];
        return g[i] + (g[i + 1] - g[i]) * fr;
    }

    void rebuildCircuit(bool resolveDc) {
        circ_.setSampleRate(fsOs_);
        circ_.configure(makeCfg(p_, fuzz_, bias_, vccNow_, driftT_, fsOs_));
        if (resolveDc) {
            if (!circ_.solveDC()) { /* leave the guess: the transient will settle it */ }
            dcTrack_ = circ_.quiescent();
        }
    }

    void control(int seg) {
        // ---- ramp: a slow hand on the knobs; it can be locked to the beat ----
        if (p_.rampSync > 0) {
            const double beats = rampSyncBeats(p_.rampSync, bpb());
            const double ph = std::fmod(beat_ / beats, 1.0);
            const double phase = ph < 0.0 ? ph + 1.0 : ph;
            if (phase < lfoPhase_ - 0.5) { lfoRandCur_ = lfoRandNext_; lfoRandNext_ = rnd(); }
            lfoPhase_ = phase;
        } else {
            const double rateHz = 0.02 * std::pow(1000.0, static_cast<double>(clamp01(p_.rampRate)));
            lfoPhase_ += rateHz * seg / fs_;
            while (lfoPhase_ >= 1.0) { lfoPhase_ -= 1.0; lfoRandCur_ = lfoRandNext_; lfoRandNext_ = rnd(); }
        }
        rampOut_ = lfoValue() * p_.rampDepth;
        const float lfo = lfoValue() * 0.5f * p_.rampDepth;
        auto mod = [&](float v, uint32_t bit) { return clamp01(v + ((p_.rampTargets & bit) ? lfo : 0.0f)); };
        const float k = segCoef(0.030, seg);
        fuzz_ += (mod(p_.fuzz, kRampFuzz) - fuzz_) * k;
        bias_ += (mod(p_.bias, kRampBias) - bias_) * k;
        tone_ += (mod(p_.tone, kRampTone) - tone_) * k;
        supplyN_ += (mod(p_.supply, kRampSupply) - supplyN_) * k;
        mix_ += (mod(p_.mix, kRampMix) - mix_) * k;
        const float ks = segCoef(0.006, seg); // the side switches fade over a few milliseconds
        octMix_ += ((p_.octave ? 1.0f : 0.0f) - octMix_) * ks;
        starveMix_ += ((p_.starve ? 1.0f : 0.0f) - starveMix_) * segCoef(0.020, seg);
        chopMix_ += ((p_.chop ? 1.0f : 0.0f) - chopMix_) * ks;
        crushMix_ += ((p_.crush ? 1.0f : 0.0f) - crushMix_) * ks;
        washFeed_ += ((p_.wash ? 1.0f : 0.0f) - washFeed_) * ks;
        swellMix_ += ((p_.swell ? 1.0f : 0.0f) - swellMix_) * ks;
        onMix_ += ((p_.on ? 1.0f : 0.0f) - onMix_) * segCoef(0.008, seg);

        // type / voicing changes: re-solve the circuit and dip the level for a few ms
        if (p_.type != lastType_ || p_.voicing != lastVoicing_) {
            const bool first = lastType_ < 0;
            lastType_ = p_.type;
            lastVoicing_ = p_.voicing;
            if (!first) duck_ = 1.0f;
            rebuildCircuit(true);
            for (auto* b : {&toneLp_, &midPk_, &shelf_}) b->reset();
        }
        // a slow wander of the temperature (germanium never sits still)
        driftT_ += rnd() * 0.0102 - driftT_ * 0.00007;
        driftT_ = std::min(1.0, std::max(-1.0, driftT_));
        // starve: a slow, uneven wobble of the battery
        sputter_ += rnd() * 0.087 - sputter_ * 0.005;
        sputter_ = std::min(1.0, std::max(-1.0, sputter_));
        // the battery
        const double vt = vccTarget(envOut_);
        vccNow_ += (vt - vccNow_) * (1.0 - std::exp(-static_cast<double>(seg) / (0.008 * fs_)));
        rebuildCircuit(false);

        // ---- pre-circuit level ----
        const VoiceDef& vo = voice(p_.voicing);
        // The fuzz knob is a drive control: the pad in front of the circuit is chosen so that the circuit's own small-signal gain
        // (which rises about 28 dB over the knob) lands on a chosen curve, kDriveLo .. kDriveHi dB end to end.
        const double drive = kDriveLo + (kDriveHi - kDriveLo) * static_cast<double>(fuzz_) + vo.trimDb;
        const double padDb = drive - refGainDb(fuzz_) - 20.0 * std::log10(kFullScaleVolts * kOutScale);
        preGain_ = dsp::dbToLin(p_.inputGainDb) * dsp::dbToLin(padDb) * kFullScaleVolts *
                   dsp::dbToLin(-34.0 * (1.0 - static_cast<double>(p_.guitar)));
        lowcutHp_.setCutoff(fs_, 20.0 * std::pow(20.0, static_cast<double>(p_.lowcut)));
        octLp_.setCutoff(fs_, 3200.0);
        octDc_.setCutoff(fs_, 60.0);

        // ---- tone ----
        const bool siT = p_.type == kTypeSi; // germanium: a little rounder and warmer; silicon: tighter and brighter
        const double fc = 1200.0 * std::pow(13.3, static_cast<double>(tone_)) * vo.toneMul * (siT ? 1.15 : 0.9);
        toneLp_.lowpass(fs_, fc, 0.707);
        midPk_.peak(fs_, 900.0, 0.9, (static_cast<double>(p_.mids) - 0.5) * 18.0 + vo.midDb);
        shelf_.lowShelf(fs_, 180.0, vo.lowShelfDb + (siT ? -1.5 : 1.0));
        dcPost_.setCutoff(fs_, 28.0);
        dcTrackA_ = 1.0 - std::exp(-2.0 * kPi * 12.0 / fsOs_);

        // ---- gain staging after the circuit ----
        const float v = clamp01(p_.volume);
        // loudness: a per-type/voicing trim (measured, see levelcal.h) and a lift at the clean end of the fuzz knob so that
        // clean, crunch and full fuzz sit at similar volumes
        const double comp = kLevelTrimDb[std::min(1, std::max(0, p_.type))][std::min(2, std::max(0, p_.voicing))] +
                            kCleanLiftDb * std::pow(1.0 - static_cast<double>(fuzz_), 1.6);
        volGain_ = 1.25f * v * v * static_cast<float>(dsp::dbToLin(p_.outputGainDb + comp));
        volRamp_.set(volGain_, seg);

        // ---- noise gate (listens to the guitar, so the fuzz can still ring out and gate by itself) ----
        {
            bool open = true;
            if (p_.gate > 0.004f) {
                const double thr = dsp::dbToLin(-72.0 + 42.0 * p_.gate);
                open = gateOpen_ ? (envInSlow_ > thr * 0.55) : (envIn_ > thr);
            }
            gateOpen_ = open;
            const float tgt = open ? 1.0f : 0.0f;
            gateGain_ += (tgt - gateGain_) * segCoef(open ? 0.002 : 0.09, seg);
            gateRamp_.set(gateGain_, seg);
        }

        // ---- chop: a rhythm of gates locked to the song ----
        {
            float open = 1.0f;
            if (p_.chop && p_.chopPattern > 0) {
                const PatternDef& pat = patternDef(p_.chopPattern);
                const double gb = stepRateBeats(p_.chopStep, bpb());
                const long long st = static_cast<long long>(std::floor(beat_ / gb));
                if (st != lastStep_) {
                    lastStep_ = st;
                    stepNow_ = static_cast<int>(((st % pat.len) + pat.len) % pat.len);
                    if (pat.randomChance > 0.0f) randOpen_ = 0.5f * (rnd() + 1.0f) < pat.randomChance;
                }
                const float pv = pat.randomChance > 0.0f ? (randOpen_ ? 1.0f : 0.0f) : pat.v[std::min(stepNow_, pat.len - 1)];
                open = 1.0f - clamp01(p_.chopDepth) * (1.0f - pv);
                const double stepSec = gb * 60.0 / bpm();
                const double rise = 0.0025 + clamp01(p_.chopSmooth) * 0.8 * stepSec;
                const double fall = 0.0025 + clamp01(p_.chopSmooth) * 0.45 * stepSec;
                pulseG_ += (open - pulseG_) * segCoef(open > pulseG_ ? rise : fall, seg);
            } else {
                pulseG_ += (1.0f - pulseG_) * segCoef(0.004, seg);
            }
            pgS_ += (pulseG_ - pgS_) * 0.35f;
            chopRamp_.set(1.0f + (pulseG_ - 1.0f) * chopMix_, seg);
        }

        // ---- crush ----
        {
            const double a = clamp01(p_.crushAmt);
            crushDiv_ = 1.0 + std::pow(a, 1.4) * 15.0;
            const double bits = 13.0 - a * 9.5;
            crushStep_ = std::pow(2.0, 1.0 - bits);
            crushLp_.setCutoff(fs_, std::min(9000.0, 0.45 * fs_ / crushDiv_));
        }
        // ---- wash ----
        reverb_.setDecay(p_.washDecay);
        // ---- swell ----
        swellT_ = 0.05 * std::pow(50.0, static_cast<double>(clamp01(p_.swellTime)));

        // ---- fade-outs after a type change ----
        duck_ = std::max(0.0f, duck_ - static_cast<float>(seg) / static_cast<float>(0.012 * fs_));
    }

    // one sample at the original rate
    void tick(float inMono, float& outL, float& outR) {
        // the dry guitar, delayed to line up with the oversampled path
        dryBuf_[dryPos_ & dryMask_] = inMono;
        const float dry = dryBuf_[(dryPos_ - latency_) & dryMask_];
        ++dryPos_;

        if (!p_.on && onMix_ < 0.0005f) { // bypassed: just the (aligned) dry guitar
            outL = outR = dry;
            meterIn_ *= 0.999f; meterOut_ *= 0.999f; clipAct_ *= 0.99f;
            return;
        }
        const double x = inMono;
        const double ax = std::fabs(x * dsp::dbToLin(p_.inputGainDb));
        envIn_ += (ax > envIn_ ? aEnvAtk_ : aEnvRel_) * (ax - envIn_) * 1.0;
        envInSlow_ += (ax > envInSlow_ ? 0.0016 : 0.0002 * 44100.0 / fs_) * (ax - envInSlow_);

        // swell: a new pick restarts a slow rise
        swFast_ += (ax > swFast_ ? 0.30 : 0.004) * (ax - swFast_);
        swSlow_ += (ax > swSlow_ ? 0.0009 : 0.00012) * (ax - swSlow_);
        if (swCool_ > 0) --swCool_;
        if (swellMix_ > 0.001f && swCool_ == 0 && swFast_ > 0.004 && swFast_ > 2.6 * swSlow_) {
            swPhase_ = 0.0;
            swActive_ = true;
            swCool_ = static_cast<int>(0.09 * fs_);
        }
        if (swActive_) {
            swPhase_ += 1.0 / (swellT_ * fs_);
            if (swPhase_ >= 1.0) { swPhase_ = 1.0; swActive_ = false; }
            swG_ = swPhase_ * swPhase_ * (3.0 - 2.0 * swPhase_);
        } else {
            swG_ = 1.0;
        }
        const float swellGainNow = 1.0f + (static_cast<float>(swG_) - 1.0f) * swellMix_;

        // ---- in front of the circuit ----
        double s = lowcutHp_.hp(x);
        if (octMix_ > 0.001f) {
            // octave: add the full-wave rectified guitar (the old "octavia" trick) before the fuzz
            const double lpS = octLp_.lp(s);
            const double rect = std::fabs(lpS);
            const double r = rect - octDc_.lp(rect);
            const double amt = static_cast<double>(octMix_) * p_.octAmt;
            s = s * (1.0 - 0.35 * amt) + 1.6 * amt * r;
        }
        const double vs = s * preGain_;

        // ---- the circuit, oversampled ----
        double railHit = 0.0;
        const double y = os_.process(vs, [&](double v) {
            const double c2 = circ_.step(v);
            dcTrack_ += dcTrackA_ * (c2 - dcTrack_);
            const double o = c2 - dcTrack_;
            railHit += (c2 > vccNow_ - 0.15 || c2 < circ_.q2Emitter() + 0.25) ? 1.0 : 0.0;
            return o;
        });
        if (!circ_.finite()) { // should never happen; recover quietly rather than screaming
            rebuildCircuit(true);
            os_.reset();
            envOut_ = 0.0;
        }
        const double fOs = static_cast<double>(os_.factor());
        clipAct_ += (static_cast<float>(railHit / fOs) - clipAct_) * 0.002f;
        double w = y * kOutScale;

        // ---- after the circuit ----
        w = dcPost_.hp(w);
        w = shelf_.process(w);
        w = midPk_.process(w);
        w = toneLp_.process(w);
        envOut_ += (std::fabs(w) > envOut_ ? 0.0015 : 0.00008 * 44100.0 / fs_) * (std::fabs(w) - envOut_);

        w *= gateRamp_.next();
        if (crushMix_ > 0.001f) {
            crushPhase_ += 1.0 / crushDiv_;
            if (crushPhase_ >= 1.0) {
                crushPhase_ -= 1.0;
                crushHold_ = std::round(w / crushStep_) * crushStep_;
            }
            const double c = crushLp_.lp(crushHold_) * 1.0;
            w += (c - w) * crushMix_;
        } else {
            crushHold_ = w;
        }
        w *= chopRamp_.next();
        w *= swellGainNow;

        float wetL = static_cast<float>(w), wetR = static_cast<float>(w);
        const bool revOn = washFeed_ > 0.0005f || reverb_.active();
        if (revOn) {
            float rl, rr;
            reverb_.process(static_cast<float>(w) * washFeed_, rl, rr);
            const float ret = 1.3f * p_.washMix;
            wetL += rl * ret;
            wetR += rr * ret;
        }
        const float vg = volRamp_.next() * (1.0f - duck_);
        wetL *= vg;
        wetR *= vg;

        // soft safety ceiling so a hot setting never leaves the converter range
        wetL = softLimit(wetL);
        wetR = softLimit(wetR);

        const float m = mix_ * onMix_;
        outL = dry * (1.0f - m) + wetL * m;
        outR = dry * (1.0f - m) + wetR * m;

        meterIn_ += (std::fabs(static_cast<float>(ax)) - meterIn_) * (std::fabs(static_cast<float>(ax)) > meterIn_ ? 0.05f : 0.0008f);
        const float ao = std::max(std::fabs(wetL), std::fabs(wetR));
        meterOut_ += (ao - meterOut_) * (ao > meterOut_ ? 0.05f : 0.0008f);
    }
    static float softLimit(float v) {
        const float a = std::fabs(v);
        if (a < 0.9f) return v;
        const float t = (a - 0.9f) / 1.1f;
        const float y = 0.9f + 1.1f * (t / (1.0f + t)) ;
        return v < 0 ? -y : y;
    }

    // ---- state ----
    Params p_;
    Transport tr_;
    double fs_ = 44100.0, fsOs_ = 176400.0;
    dsp::Oversampler os_;
    Circuit circ_;
    dsp::Reverb reverb_;
    int latency_ = 0;
    std::vector<float> dryBuf_;
    int dryMask_ = 0, dryPos_ = 0;
    int untilControl_ = 0;
    uint32_t rng_ = 0x9E3779B9u;

    // smoothed controls
    float fuzz_ = 0.6f, bias_ = 0.5f, tone_ = 0.55f, supplyN_ = 0.88f, mix_ = 1.0f;
    float octMix_ = 0, starveMix_ = 0, chopMix_ = 0, crushMix_ = 0, washFeed_ = 0, swellMix_ = 0, onMix_ = 1.0f;
    double vccNow_ = 9.0;
    double driftT_ = 0, sputter_ = 0;
    int lastType_ = -1, lastVoicing_ = -1;
    double preGain_ = 0.25;
    float volGain_ = 0.3f;
    dsp::LinRamp volRamp_, gateRamp_, chopRamp_;
    float gateGain_ = 1.0f;
    bool gateOpen_ = true;
    float duck_ = 0.0f;

    // filters and detectors
    dsp::OnePole lowcutHp_, dcPost_, crushLp_, octLp_, octDc_;
    dsp::Biquad toneLp_, midPk_, shelf_;
    double dcTrack_ = 4.5, dcTrackA_ = 1e-4;
    double envIn_ = 0, envInSlow_ = 0, envOut_ = 0;
    double aEnvAtk_ = 0.05, aEnvRel_ = 0.002;
    double swFast_ = 0, swSlow_ = 0, swPhase_ = 1.0, swG_ = 1.0, swellT_ = 0.5;
    bool swActive_ = false;
    int swCool_ = 0;
    double crushHold_ = 0, crushPhase_ = 0, crushDiv_ = 1, crushStep_ = 1e-3;

    // tempo
    double beat_ = 0, beatFree_ = 0, lfoPhase_ = 0;
    float lfoRandCur_ = 0, lfoRandNext_ = 0, rampOut_ = 0;
    long long lastStep_ = -(1LL << 30);
    int stepNow_ = 0;
    bool randOpen_ = true;
    float pulseG_ = 1.0f, pgS_ = 1.0f;

    double gainTable_[2][3][kGainPoints] = {};

    // meters
    float meterIn_ = 0, meterOut_ = 0, clipAct_ = 0;
};

} // namespace verdigris
