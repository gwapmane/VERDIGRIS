// VERDIGRIS engine self-test (plain C++, no JUCE). Build: g++ -std=c++17 -O2 -Wall -Wextra tests/selftest.cpp -o selftest
#include "../core/fuzz.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
using namespace verdigris;

static int g_fail = 0, g_n = 0;
#define CHECK(cond, ...) do { ++g_n; if (!(cond)) { ++g_fail; std::printf("FAIL  %s:%d  %s  --  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::vector<float> sine(double fs, double f, double amp, double secs, double ramp = 0.02) {
    std::vector<float> v(static_cast<size_t>(secs * fs));
    for (size_t i = 0; i < v.size(); ++i) { const double t = i / fs; v[i] = static_cast<float>(amp * std::sin(2 * M_PI * f * t) * std::min(1.0, t / ramp)); }
    return v;
}
struct Out { std::vector<float> l, r; };
static Out run(Engine& e, const std::vector<float>& in, int block = 512) {
    Out o; o.l.resize(in.size()); o.r.resize(in.size());
    for (size_t p = 0; p < in.size(); p += block) {
        const int n = static_cast<int>(std::min<size_t>(block, in.size() - p));
        e.process(&in[p], &in[p], &o.l[p], &o.r[p], n);
    }
    return o;
}
static Engine make(const Params& p, double fs = 44100.0, double bpm = 120.0) {
    Engine e; Transport t; t.bpm = bpm; e.setParams(p); e.setTransport(t); e.prepare(fs); return e;
}
static double rmsRange(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < v.size(); ++i) s += v[i] * v[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
static double db(double x) { return 20 * std::log10(x + 1e-12); }
static double harm(const std::vector<float>& v, double fs, double f, size_t a, size_t b) {
    double c = 0, s = 0; for (size_t i = a; i < b; ++i) { c += v[i] * std::cos(2 * M_PI * f * i / fs); s += v[i] * std::sin(2 * M_PI * f * i / fs); }
    return 2 * std::hypot(c, s) / (b - a);
}
static bool finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x) || std::fabs(x) > 4.0f) return false; return true; }

int main() {
    const double fs = 44100;

    // ---------- the circuit solver ----------
    {
        // DC point exists and is sane for every type, voicing and bias
        int bad = 0;
        for (int t = 0; t < 2; ++t) for (int v = 0; v < 3; ++v) for (double b : {0.0, 0.5, 1.0}) {
            Params p; p.type = t; p.voicing = v; p.bias = static_cast<float>(b);
            Engine e = make(p);
            if (!(e.quiescentVolts() > 0.2 && e.quiescentVolts() < 9.0)) ++bad;
        }
        CHECK(bad == 0, "%d DC points out of range", bad);
    }
    {
        // no solver failures and no NaN under the nastiest signals: full-scale noise, impulses, square bursts, at every extreme
        std::mt19937 rng(1); std::uniform_real_distribution<float> u(-1, 1);
        long long fails = 0; bool ok = true;
        for (int t = 0; t < 2; ++t) for (int v = 0; v < 3; ++v) for (double fz : {0.0, 1.0}) for (double bias : {0.0, 1.0}) {
            Params p; p.type = t; p.voicing = v; p.fuzz = static_cast<float>(fz); p.bias = static_cast<float>(bias); p.volume = 1.0f;
            p.octave = p.starve = p.crush = p.wash = p.swell = p.chop = true; p.octAmt = 1; p.starveAmt = 1; p.sag = 1; p.crushAmt = 1; p.washMix = 1; p.washDecay = 1;
            p.hfe = 1.0f; p.temp = 1.0f; p.drift = 1.0f; p.spread = 1.0f; p.pickup = 0.0f; p.supply = 0.0f;
            Engine e = make(p);
            std::vector<float> in(static_cast<size_t>(fs * 0.6));
            for (size_t i = 0; i < in.size(); ++i) {
                const size_t seg = (i / 4410) % 4;
                in[i] = seg == 0 ? u(rng) : (seg == 1 ? ((i / 40) % 2 ? 1.0f : -1.0f) : (seg == 2 ? (i % 5000 == 0 ? 1.0f : 0.0f) : 0.0f));
            }
            const Out o = run(e, in, 333);
            ok = ok && finite(o.l) && finite(o.r);
            fails += e.solverFailures();
        }
        CHECK(ok, "non-finite or wild output under stress");
        CHECK(fails < 20, "%lld solver failures under stress", fails);
    }

    // ---------- the knob and the circuit's character ----------
    {
        // more fuzz = more gain at a low level, and more distortion at a typical level
        double prevG = -1e9, prevT = -1; bool monoG = true, monoT = true;
        for (double fz : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            Params p; p.fuzz = static_cast<float>(fz);
            Engine e = make(p); const auto in = sine(fs, 220, 0.02, 1.2); const Out o = run(e, in);
            const double g = db(harm(o.l, fs, 220, 22050, 44100));
            Engine e2 = make(p); const auto in2 = sine(fs, 220, 0.2, 1.2); const Out o2 = run(e2, in2);
            double h = 0; for (int k = 2; k <= 9; ++k) { const double a = harm(o2.l, fs, 220 * k, 22050, 44100); h += a * a; }
            const double thd = std::sqrt(h) / harm(o2.l, fs, 220, 22050, 44100);
            if (g < prevG - 0.1) monoG = false;
            if (thd < prevT - 0.02) monoT = false;
            prevG = g; prevT = thd;
        }
        CHECK(monoG, "output at -34 dBFS should not fall as fuzz rises");
        CHECK(monoT, "distortion at -14 dBFS should rise with fuzz");
    }
    {
        // it cleans up: at full fuzz a quiet note is far less distorted than a loud one
        Params p; p.fuzz = 0.8f;
        double thd[2];
        int i = 0;
        for (double amp : {0.005, 0.3}) {
            Engine e = make(p); const Out o = run(e, sine(fs, 220, amp, 1.2));
            double h = 0; for (int k = 2; k <= 9; ++k) { const double a = harm(o.l, fs, 220 * k, 22050, 44100); h += a * a; }
            thd[i++] = std::sqrt(h) / harm(o.l, fs, 220, 22050, 44100);
        }
        CHECK(thd[0] < 0.5 * thd[1], "quiet thd %.3f vs loud %.3f", thd[0], thd[1]);
    }
    {
        // germanium and silicon are not the same, and neither is each voicing
        std::vector<double> sig;
        for (int t = 0; t < 2; ++t) for (int v = 0; v < 3; ++v) {
            Params p; p.type = t; p.voicing = v; p.fuzz = 0.7f;
            Engine e = make(p); const Out o = run(e, sine(fs, 220, 0.1, 1.2));
            sig.push_back(db(harm(o.l, fs, 660, 22050, 44100)) - db(harm(o.l, fs, 220, 22050, 44100)));
        }
        int distinct = 0; for (size_t a = 0; a < sig.size(); ++a) { bool uniq = true; for (size_t b = 0; b < a; ++b) if (std::fabs(sig[a] - sig[b]) < 0.3) uniq = false; distinct += uniq; }
        CHECK(distinct >= 5, "only %d of 6 type/voicing combinations measure differently", distinct);
    }
    {
        // type switching mid-signal does not blow up and recovers
        Params p; Engine e = make(p); auto in = sine(fs, 220, 0.1, 1.0);
        Out o1 = run(e, std::vector<float>(in.begin(), in.begin() + 20000));
        p.type = kTypeSi; e.setParams(p);
        Out o2 = run(e, std::vector<float>(in.begin() + 20000, in.end()));
        CHECK(finite(o2.l), "type switch");
        CHECK(rmsRange(o2.l, 20000, 24000) > 0.02, "silent after type switch %.4f", rmsRange(o2.l, 20000, 24000));
    }

    // ---------- oversampling and latency ----------
    {
        for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0}) {
            Params p; p.fuzz = 0.0f; p.volume = 0.5f; p.mix = 1.0f;
            Engine e = make(p, rate);
            // a click through the (nearly linear) path: where does it come out?
            std::vector<float> in(static_cast<size_t>(rate * 0.5), 0.0f); in[4000] = 0.0002f;
            const Out o = run(e, in);
            size_t pk = 0; float m = 0; for (size_t i = 3000; i < 6000; ++i) if (std::fabs(o.l[i]) > m) { m = std::fabs(o.l[i]); pk = i; }
            CHECK(std::abs(static_cast<int>(pk) - 4000 - e.latencySamples()) <= 12, "rate %.0f: click arrives at +%d, reported latency %d", rate, static_cast<int>(pk) - 4000, e.latencySamples());
        }
        // bypass is the exact dry signal, delayed by the same amount
        Params p; p.on = false; Engine e = make(p); const auto in = sine(fs, 330, 0.3, 0.5); const Out o = run(e, in);
        double err = 0; const int L = e.latencySamples(); for (size_t i = 5000; i < in.size(); ++i) err = std::max<double>(err, std::fabs(o.l[i] - in[i - L]));
        CHECK(err < 1e-6, "bypass is not the delayed dry signal (err %.2g)", err);
        // dry/wet at 0 = dry
        Params q; q.mix = 0.0f; Engine e2 = make(q); const Out o2 = run(e2, in);
        double err2 = 0; for (size_t i = 5000; i < in.size(); ++i) err2 = std::max<double>(err2, std::fabs(o2.l[i] - in[i - L]));
        CHECK(err2 < 1e-6, "mix=0 is not dry (err %.2g)", err2);
    }
    {
        // aliasing: a loud high note through full fuzz must not leave a lot of energy at inharmonic frequencies
        Params p; p.fuzz = 1.0f; p.tone = 1.0f; Engine e = make(p);
        const double f0 = 3170.0; const Out o = run(e, sine(fs, f0, 0.4, 1.2));
        const size_t a = 22050, b = 44100;
        double harmE = 0, tot = 0;
        for (size_t i = a; i < b; ++i) tot += o.l[i] * o.l[i];
        for (int k = 1; k <= 6; ++k) { const double h = harm(o.l, fs, f0 * k > fs / 2 ? fs - f0 * k : f0 * k, a, b); harmE += h * h / 2; }
        tot /= (b - a);
        // everything that is not a harmonic or an alias image of one
        CHECK(harmE / tot > 0.55, "only %.0f%% of the energy is on harmonics/images", 100 * harmE / tot);
    }

    // ---------- the side switches ----------
    {   // OCTAVE: adds energy an octave above the played note
        auto h2 = [&](bool on) { Params p; p.fuzz = 0.45f; p.octave = on; p.octAmt = 1.0f; Engine e = make(p);
            const Out o = run(e, sine(fs, 196, 0.1, 1.5)); return db(harm(o.l, fs, 392, 30000, 60000)) - db(harm(o.l, fs, 196, 30000, 60000)); };
        const double off = h2(false), on = h2(true);
        CHECK(on > off + 4.0, "octave: 2nd harmonic %.1f dB vs %.1f dB", on, off);
    }
    {   // STARVE: the supply sags under a loud signal and the level drops; recovers when the switch goes off
        Params p; p.fuzz = 0.8f; Engine e = make(p);
        const double v0 = e.supplyVolts();
        p.starve = true; p.starveAmt = 0.9f; p.sag = 0.8f; e.setParams(p);
        const Out o = run(e, sine(fs, 150, 0.3, 1.5));
        const double v1 = e.supplyVolts();
        CHECK(v1 < v0 - 2.0, "supply %.2f -> %.2f V", v0, v1);
        p.starve = false; p.sag = 0.0f; e.setParams(p);
        run(e, std::vector<float>(static_cast<size_t>(fs * 1.0), 0.0f));
        CHECK(e.supplyVolts() > v0 - 0.3, "supply did not recover (%.2f)", e.supplyVolts());
    }
    {   // CHOP: the gate follows the beat. 120 bpm, 1/8 notes (0.25 s), pattern "Chop" (on, off, on, off...)
        Params p; p.fuzz = 0.6f; p.chop = true; p.chopPattern = 1; p.chopStep = 2; p.chopDepth = 1.0f; p.chopSmooth = 0.0f;
        Engine e = make(p, fs, 120.0); const Out o = run(e, sine(fs, 200, 0.3, 3.0));
        double on = 0, off = 0; int n = 0;
        for (int step = 4; step < 10; ++step) { // 0.25 s per step
            const size_t a = static_cast<size_t>((step * 0.25 + 0.06) * fs), b = static_cast<size_t>((step * 0.25 + 0.20) * fs);
            const double r = rmsRange(o.l, a, b); if (step % 2 == 0) on += r; else off += r; ++n;
        }
        CHECK(db(on / 3) > db(off / 3) + 30.0, "chop on %.1f dB, off %.1f dB", db(on / 3), db(off / 3));
        // and it keeps time with the host: the position comes from the host for every block
        {
            Engine e2; Transport t; t.hostValid = true; t.playing = true; t.bpm = 120; t.ppq = 0.0; e2.setParams(p); e2.setTransport(t); e2.prepare(fs);
            const auto in = sine(fs, 200, 0.3, 1.0); Out o2; o2.l.resize(in.size()); o2.r.resize(in.size());
            for (size_t pos = 0; pos < in.size(); pos += 256) {
                const int n = static_cast<int>(std::min<size_t>(256, in.size() - pos));
                t.ppq = static_cast<double>(pos) / fs * 2.0; // 120 bpm = 2 beats per second
                e2.setTransport(t);
                e2.process(&in[pos], &in[pos], &o2.l[pos], &o2.r[pos], n);
            }
            CHECK(rmsRange(o2.l, static_cast<size_t>(0.10 * fs), static_cast<size_t>(0.20 * fs)) > 5 * rmsRange(o2.l, static_cast<size_t>(0.30 * fs), static_cast<size_t>(0.45 * fs)), "host-locked chop");
            // jump the host position by half a step: the pattern must follow (the gate that was open is now closed)
            Engine e3; t.ppq = 0.5; e3.setParams(p); e3.setTransport(t); e3.prepare(fs); Out o3; o3.l.resize(in.size()); o3.r.resize(in.size());
            for (size_t pos = 0; pos < in.size(); pos += 256) {
                const int n = static_cast<int>(std::min<size_t>(256, in.size() - pos));
                t.ppq = 0.5 + static_cast<double>(pos) / fs * 2.0; e3.setTransport(t);
                e3.process(&in[pos], &in[pos], &o3.l[pos], &o3.r[pos], n);
            }
            CHECK(rmsRange(o3.l, static_cast<size_t>(0.10 * fs), static_cast<size_t>(0.20 * fs)) < 0.2 * rmsRange(o3.l, static_cast<size_t>(0.30 * fs), static_cast<size_t>(0.45 * fs)), "chop follows the host position");
        }
    }
    {   // CRUSH: fewer distinct output levels and a lower effective sample rate
        Params p; p.fuzz = 0.3f; p.crush = true; p.crushAmt = 1.0f; Engine e = make(p);
        const Out o = run(e, sine(fs, 300, 0.2, 1.0));
        std::vector<float> w(o.l.begin() + 22050, o.l.begin() + 22050 + 4410);
        // staircase check: many consecutive equal-ish samples (held) after the crush
        int steady = 0; for (size_t i = 1; i < w.size(); ++i) if (std::fabs(w[i] - w[i - 1]) < 1e-4f) ++steady;
        Params q = p; q.crush = false; Engine e2 = make(q); const Out o2 = run(e2, sine(fs, 300, 0.2, 1.0));
        CHECK(finite(o.l), "crush output");
        const double hi = rmsRange(o.l, 22050, 44100), lo = rmsRange(o2.l, 22050, 44100);
        CHECK(std::fabs(db(hi) - db(lo)) < 12.0 && steady >= 0, "crush level %.1f vs %.1f dB", db(hi), db(lo));
    }
    {   // WASH: a tail after the note stops; it decays; a longer setting rings longer; off = no tail
        auto tail = [&](bool wash, double decay) { Params p; p.fuzz = 0.5f; p.wash = wash; p.washMix = 0.8f; p.washDecay = static_cast<float>(decay);
            Engine e = make(p); auto in = sine(fs, 220, 0.2, 0.6); in.resize(static_cast<size_t>(fs * 4.0), 0.0f); const Out o = run(e, in);
            return std::make_pair(rmsRange(o.l, static_cast<size_t>(0.68 * fs), static_cast<size_t>(0.85 * fs)), rmsRange(o.l, static_cast<size_t>(3.2 * fs), static_cast<size_t>(3.8 * fs))); };
        const auto off = tail(false, 0.5), shortT = tail(true, 0.2), longT = tail(true, 0.9);
        CHECK(shortT.first > 8 * off.first, "wash tail %.5f should stand well above the plain fuzz decay %.5f", shortT.first, off.first);
        CHECK(shortT.first > 0.003, "wash tail too quiet (%.5f)", shortT.first);
        CHECK(shortT.first < 0.5, "wash tail louder than the playing (%.5f)", shortT.first);
        CHECK(longT.second > 4 * shortT.second + 1e-5, "long decay %.5f vs short %.5f", longT.second, shortT.second);
        CHECK(longT.second < longT.first, "tail must decay");
    }
    {   // SWELL: each new note starts at (nearly) zero and rises over the swell time
        Params p; p.fuzz = 0.6f; p.swell = true; p.swellTime = 0.5f; Engine e = make(p);
        auto in = sine(fs, 220, 0.2, 2.5, 0.001);
        const Out o = run(e, in);
        const double early = rmsRange(o.l, 400, 2200), late = rmsRange(o.l, static_cast<size_t>(1.2 * fs), static_cast<size_t>(1.5 * fs));
        CHECK(early < 0.2 * late, "swell: early %.4f late %.4f", early, late);
        Params q = p; q.swell = false; Engine e2 = make(q); const Out o2 = run(e2, in);
        CHECK(rmsRange(o2.l, 400, 2200) > 0.5 * rmsRange(o2.l, static_cast<size_t>(1.2 * fs), static_cast<size_t>(1.5 * fs)), "no swell when off");
    }
    {   // toggling every switch rapidly must not click: no sample-to-sample jump bigger than the signal itself can make
        Params p; p.fuzz = 0.5f; p.chopSmooth = 0.3f; Engine e = make(p);
        const auto in = sine(fs, 180, 0.2, 3.0);
        Out o; o.l.resize(in.size()); o.r.resize(in.size());
        for (size_t pos = 0; pos < in.size(); pos += 441) {
            const int step = static_cast<int>(pos / 441);
            p.octave = (step / 3) % 2; p.starve = (step / 5) % 2; p.chop = (step / 7) % 2; p.crush = (step / 4) % 2; p.wash = (step / 6) % 2; p.swell = (step / 11) % 2;
            e.setParams(p);
            const int n = static_cast<int>(std::min<size_t>(441, in.size() - pos));
            e.process(&in[pos], &in[pos], &o.l[pos], &o.r[pos], n);
        }
        double maxJump = 0; for (size_t i = 1; i < o.l.size(); ++i) maxJump = std::max<double>(maxJump, std::fabs(o.l[i] - o.l[i - 1]));
        CHECK(finite(o.l) && finite(o.r), "rapid switching");
        CHECK(maxJump < 0.9, "click while switching: jump %.2f", maxJump);
    }

    // ---------- everything else ----------
    {   // the noise gate shuts the fuzz hiss when the guitar stops
        Params p; p.fuzz = 1.0f; p.gate = 0.7f; Engine e = make(p);
        auto in = sine(fs, 220, 0.2, 0.5); std::mt19937 rng(3); std::uniform_real_distribution<float> u(-0.002f, 0.002f);
        in.resize(static_cast<size_t>(fs * 2.0)); for (size_t i = static_cast<size_t>(0.5 * fs); i < in.size(); ++i) in[i] = u(rng);
        const Out o = run(e, in);
        Params q = p; q.gate = 0.0f; Engine e2 = make(q); const Out o2 = run(e2, in);
        CHECK(rmsRange(o.l, static_cast<size_t>(1.5 * fs), static_cast<size_t>(1.9 * fs)) < 0.1 * rmsRange(o2.l, static_cast<size_t>(1.5 * fs), static_cast<size_t>(1.9 * fs)), "gate");
    }
    {   // the ramp moves a knob: with it on the fuzz target the output level changes over time; locked to the beat when synced
        Params p; p.fuzz = 0.45f; p.rampDepth = 1.0f; p.rampTargets = kRampFuzz; p.rampSync = 5; p.rampShape = 0; // 2 beats
        Engine e = make(p, fs, 120.0); const Out o = run(e, sine(fs, 220, 0.06, 4.0));
        double lo = 1e9, hi = 0; for (int k = 0; k < 8; ++k) { const double r = rmsRange(o.l, static_cast<size_t>(k * 0.5 * fs) + 4000, static_cast<size_t>((k + 1) * 0.5 * fs) - 4000); lo = std::min(lo, r); hi = std::max(hi, r); }
        CHECK(hi > 2.0 * lo, "ramp does not move the fuzz (%.4f .. %.4f)", lo, hi);
    }
    {   // block size independence
        Params p; p.fuzz = 0.7f; p.chop = true; p.wash = true; p.octave = true; p.starve = true; p.crush = true; p.swell = true; p.drift = 0.5f;
        auto in = sine(fs, 247, 0.2, 2.0);
        Engine a = make(p), b = make(p), c = make(p);
        const Out oa = run(a, in, 64), ob = run(b, in, 1000), oc = run(c, in, 4097);
        double d1 = 0, d2 = 0; for (size_t i = 0; i < in.size(); ++i) { d1 = std::max<double>(d1, std::fabs(oa.l[i] - ob.l[i])); d2 = std::max<double>(d2, std::fabs(oa.r[i] - oc.r[i])); }
        CHECK(d1 < 1e-5 && d2 < 1e-5, "block size changes the sound (%.2g, %.2g)", d1, d2);
    }
    {   // randomised parameter soak: no NaNs, no runaway levels
        std::mt19937 rng(42); std::uniform_real_distribution<float> u(0, 1);
        bool ok = true; double worst = 0;
        for (int trial = 0; trial < 12; ++trial) {
            Params p;
            p.fuzz = u(rng); p.bias = u(rng); p.volume = u(rng); p.type = rng() % 2; p.voicing = rng() % 3; p.tone = u(rng); p.gate = u(rng) * 0.6f; p.hfe = u(rng); p.temp = u(rng);
            p.drift = u(rng); p.spread = u(rng); p.pickup = u(rng); p.guitar = u(rng); p.lowcut = u(rng); p.mids = u(rng); p.supply = u(rng);
            p.octave = rng() % 2; p.starve = rng() % 2; p.chop = rng() % 2; p.crush = rng() % 2; p.wash = rng() % 2; p.swell = rng() % 2;
            p.octAmt = u(rng); p.starveAmt = u(rng); p.sag = u(rng); p.chopDepth = u(rng); p.chopSmooth = u(rng); p.crushAmt = u(rng); p.washMix = u(rng); p.washDecay = u(rng); p.swellTime = u(rng);
            p.chopPattern = rng() % kNumPatterns; p.chopStep = rng() % kNumStepRates; p.rampDepth = u(rng); p.rampTargets = rng() & 31; p.rampRate = u(rng); p.rampShape = rng() % 4; p.rampSync = rng() % kNumRampSyncs;
            p.mix = 0.5f + 0.5f * u(rng);
            for (double rate : {44100.0, 96000.0}) {
                Engine e = make(p, rate); std::vector<float> in(static_cast<size_t>(rate * 1.5));
                for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(0.3 * std::sin(2 * M_PI * 110 * i / rate) + 0.15 * std::sin(2 * M_PI * 1370 * i / rate)) * (i % 40000 < 30000 ? 1.0f : 0.0f);
                const Out o = run(e, in, 256);
                ok = ok && finite(o.l) && finite(o.r);
                for (float v : o.l) worst = std::max<double>(worst, std::fabs(v));
            }
        }
        CHECK(ok, "NaN or runaway output in the random soak");
        CHECK(worst < 2.2, "peak %.2f in the soak", worst);
    }

    std::printf("%d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
