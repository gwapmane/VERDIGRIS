// VERDIGRIS - the fuzz circuit. Plain C++ (no JUCE).
//
// A two-transistor fuzz of the classic kind, solved for real, sample by sample:
//
//   guitar --Rs--C1--> [Q1 base]            Q1: common emitter, collector load R1, collector goes straight into Q2's base
//   [Q2 emitter] --R3 (100k)--> [Q1 base]   R3: feeds back from Q2's emitter, and sets the whole DC operating point
//   Q2: collector load Rc2 (this is where the output is taken); emitter goes to ground through the FUZZ pot (1k),
//       whose wiper is bypassed to ground by a big capacitor: at one end of the pot the bypass does nothing (the full
//       1k degenerates the stage: clean), at the other end it bypasses everything (full gain: fuzz).
//
// The transistors follow Ebers-Moll with the leakage current germanium parts have (it doubles every few degrees, so
// the bias drifts with temperature). Every sample, Newton's method finds the four node voltages (Q1 base, Q1 collector
// / Q2 base, Q2 collector, Q2 emitter); the two capacitors are handled with backward Euler. Because the circuit is
// really being solved, the things that make a fuzz alive come for free: it cleans up when the input gets quieter, it
// loads the guitar, the bias point moves the clipping from soft to gated and spitty, and the supply voltage matters.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>

namespace verdigris {

struct Bjt {          // NPN Ebers-Moll (the real pedal is PNP on a positive ground: the same thing, mirrored)
    double Is = 1e-14;  // saturation current
    double nVt = 0.0259; // emission coefficient * thermal voltage
    double bf = 100.0, br = 5.0; // forward / reverse current gain
    double icbo = 0.0;  // collector-base leakage
};

struct CircuitCfg {
    double vcc = 9.0;
    double r1 = 33e3, r3 = 100e3, rc2 = 8670.0;
    double rpot = 1000.0;       // the fuzz pot
    double fuzz = 1.0;          // 0 = clean (pot fully in the emitter), 1 = fully bypassed
    double c1 = 2.2e-6, cb = 20e-6;
    double rs = 6000.0;         // what the guitar looks like from the pedal
    Bjt q1, q2;
};

class Circuit {
public:
    void setSampleRate(double fs) { h_ = 1.0 / fs; }
    void configure(const CircuitCfg& c) {
        cfg_ = c;
        // the fuzz pot: a series resistor (Rdeg) in the emitter that the bypass cap cannot short, and the rest (Rbot)
        const double f = std::min(1.0, std::max(0.0, c.fuzz));
        const double taper = (1.0 - f) * (1.0 - f);
        rdeg_ = std::max(0.5, c.rpot * taper);
        rbot_ = std::max(0.5, c.rpot - rdeg_ + 0.5);
        gin_ = 1.0 / (c.rs + h_ / c.c1);
        gc_ = c.cb / h_;
        gtot_ = 1.0 / rdeg_ + 1.0 / rbot_ + gc_;
        geq_ = (1.0 / rdeg_) * (1.0 - (1.0 / rdeg_) / gtot_);
    }

    // find the resting point (no signal, capacitors charged)
    bool solveDC() {
        double x[4] = {cfg_.q1.nVt * 9.0, cfg_.q1.nVt * 18.0, cfg_.vcc * 0.5, cfg_.q1.nVt * 14.0};
        // a better first guess for silicon
        if (cfg_.q1.Is < 1e-11) { x[0] = 0.60; x[1] = 1.0; x[3] = 0.4; }
        B1_ = x[0]; B2_ = x[1]; C2_ = x[2]; E2_ = x[3];
        bool ok = false;
        for (int pass = 0; pass < 3 && !ok; ++pass) {
            ok = newton(0.0, true, 200);
            if (!ok) { B1_ = 0.5 * (pass + 1) * (cfg_.q1.Is < 1e-11 ? 0.6 : 0.25); B2_ = B1_ + 0.4; C2_ = cfg_.vcc * 0.5; E2_ = 0.3; }
        }
        vcIn_ = -B1_;
        vW_ = E2_ * rbot_ / (rdeg_ + rbot_);
        quiescentC2_ = C2_;
        return ok;
    }

    // one sample: the guitar's open-circuit voltage in, the voltage at Q2's collector out
    double step(double vs) {
        iters_ = 0;
        const double iinSrc = gin_ * (vs - vcIn_);
        // predictor: carry on in the direction the node was already moving
        const bool ok = newton(iinSrc, false, 30);
        if (!ok) ++fails_;
        const double iin = iinSrc - gin_ * B1_;
        vcIn_ += iin * h_ / cfg_.c1;
        vW_ = (E2_ / rdeg_ + gc_ * vW_) / gtot_;
        return C2_;
    }

    // Small-signal gain (volts at Q2's collector per volt from the guitar's open circuit). With freq <= 0 both capacitors are
    // ideal shorts; otherwise the gain is worked out at that frequency, which is what a note really sees: with the fuzz up
    // full, the bypass capacitor stops being a short below a few hundred Hz and the bass is less fuzzed. Works on its own
    // copy of the state, so the running circuit is not disturbed.
    double acGain(double freq = -1.0) {
        const double sB1 = B1_, sB2 = B2_, sC2 = C2_, sE2 = E2_, sQ = quiescentC2_, sc = vcIn_, sw = vW_;
        const int sIt = iters_;
        double gain = 0.0;
        if (solveDC()) {
            using cd = std::complex<double>;
            double J[4][5], F[4];
            assemble(0.0, 0.0, 0.0, 0.0, J, F); // the transistor and resistor part; the emitter and input terms are added below
            cd gin, ye;
            if (freq <= 0.0) {
                gin = 1.0 / cfg_.rs;
                ye = 1.0 / rdeg_;
            } else {
                const double w = 2.0 * 3.14159265358979323846 * freq;
                gin = 1.0 / (cd(cfg_.rs, 0.0) + cd(0.0, -1.0 / (w * cfg_.c1)));
                const cd yw = cd(1.0 / rbot_, w * cfg_.cb); // the wiper: the rest of the pot in parallel with the bypass capacitor
                ye = 1.0 / (cd(rdeg_, 0.0) + 1.0 / yw);
            }
            cd M[4][5];
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) M[r][c] = J[r][c];
            M[0][0] += gin;
            M[3][3] += ye;
            for (int r = 0; r < 4; ++r) M[r][4] = (r == 0) ? gin : cd(0.0, 0.0);
            bool ok = true;
            for (int c = 0; c < 4 && ok; ++c) {
                int pv = c;
                for (int r = c + 1; r < 4; ++r) if (std::abs(M[r][c]) > std::abs(M[pv][c])) pv = r;
                if (std::abs(M[pv][c]) < 1e-30) { ok = false; break; }
                if (pv != c) for (int k = c; k < 5; ++k) std::swap(M[pv][k], M[c][k]);
                for (int r = c + 1; r < 4; ++r) {
                    const cd m = M[r][c] / M[c][c];
                    for (int k = c; k < 5; ++k) M[r][k] -= m * M[c][k];
                }
            }
            if (ok) {
                cd d[4];
                for (int r = 3; r >= 0; --r) {
                    cd v = M[r][4];
                    for (int k = r + 1; k < 4; ++k) v -= M[r][k] * d[k];
                    d[r] = v / M[r][r];
                }
                gain = std::abs(d[2]);
            }
        }
        B1_ = sB1; B2_ = sB2; C2_ = sC2; E2_ = sE2; quiescentC2_ = sQ; vcIn_ = sc; vW_ = sw; iters_ = sIt;
        return gain;
    }

    double quiescent() const { return quiescentC2_; }
    double q2Base() const { return B2_; }
    double q1Collector() const { return B2_; }
    double q2Collector() const { return C2_; }
    double q2Emitter() const { return E2_; }
    double q1Base() const { return B1_; }
    int lastIterations() const { return iters_; }
    double lastDm() const { return lastDm_; }
    double lastAlpha() const { return lastAlpha_; }
    long long failures() const { return fails_; }
    void clearStats() { fails_ = 0; }
    // used when parameters change a lot at once (a bias or type switch): make sure the state is sane
    bool finite() const { return std::isfinite(B1_) && std::isfinite(B2_) && std::isfinite(C2_) && std::isfinite(E2_) && std::isfinite(vcIn_) && std::isfinite(vW_); }

private:
    struct E { double ic, ib, ie, gf, gr; };
    static void eval(const Bjt& q, double vbe, double vbc, E& e) {
        const double ef = std::exp(std::min(vbe / q.nVt, 45.0)), er = std::exp(std::min(vbc / q.nVt, 45.0));
        const double If = q.Is * (ef - 1.0), Ir = q.Is * (er - 1.0);
        e.gf = q.Is * ef / q.nVt;
        e.gr = q.Is * er / q.nVt;
        e.ic = If - Ir * (1.0 + 1.0 / q.br) + q.icbo;
        e.ib = If / q.bf + Ir / q.br - q.icbo;
        e.ie = If * (1.0 + 1.0 / q.bf) - Ir;
    }

    // the circuit equations and their derivatives at the current node voltages
    // (rows: Q1 base, Q1 collector / Q2 base, Q2 collector, Q2 emitter; the last column is -F)
    void assemble(double geq, double ieq, double gin, double iinSrc, double J[4][5], double F[4]) const {
        const double ir3 = 1.0 / cfg_.r3, ir1 = 1.0 / cfg_.r1, irc = 1.0 / cfg_.rc2;
        const Bjt& q1 = cfg_.q1;
        const Bjt& q2 = cfg_.q2;
        E a, b;
        eval(q1, B1_, B1_ - B2_, a);
        eval(q2, B2_ - E2_, B2_ - C2_, b);
        F[0] = a.ib + (B1_ - E2_) * ir3 - (iinSrc - gin * B1_);
        F[1] = a.ic + b.ib + (B2_ - cfg_.vcc) * ir1;
        F[2] = b.ic + (C2_ - cfg_.vcc) * irc;
        F[3] = -b.ie + (E2_ - B1_) * ir3 + geq * E2_ - ieq;
        const double a_ib_be = a.gf / q1.bf, a_ib_bc = a.gr / q1.br;
        const double a_ic_be = a.gf, a_ic_bc = -a.gr * (1.0 + 1.0 / q1.br);
        const double b_ib_be = b.gf / q2.bf, b_ib_bc = b.gr / q2.br;
        const double b_ic_be = b.gf, b_ic_bc = -b.gr * (1.0 + 1.0 / q2.br);
        const double b_ie_be = b.gf * (1.0 + 1.0 / q2.bf), b_ie_bc = -b.gr;
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 5; ++c) J[r][c] = 0.0;
        J[0][0] = a_ib_be + a_ib_bc + ir3 + gin;
        J[0][1] = -a_ib_bc;
        J[0][3] = -ir3;
        J[1][0] = a_ic_be + a_ic_bc;
        J[1][1] = -a_ic_bc + (b_ib_be + b_ib_bc) + ir1;
        J[1][2] = -b_ib_bc;
        J[1][3] = -b_ib_be;
        J[2][1] = b_ic_be + b_ic_bc;
        J[2][2] = -b_ic_bc + irc;
        J[2][3] = -b_ic_be;
        J[3][0] = -ir3;
        J[3][1] = -(b_ie_be + b_ie_bc);
        J[3][2] = b_ie_bc;
        J[3][3] = b_ie_be + ir3 + geq;
        for (int r = 0; r < 4; ++r) J[r][4] = -F[r];
    }
    static bool solve4(double J[4][5], double d[4]) {
        for (int c = 0; c < 4; ++c) {
            int p = c;
            for (int r = c + 1; r < 4; ++r) if (std::fabs(J[r][c]) > std::fabs(J[p][c])) p = r;
            if (std::fabs(J[p][c]) < 1e-30) return false;
            if (p != c) for (int k = c; k < 5; ++k) std::swap(J[p][k], J[c][k]);
            for (int r = c + 1; r < 4; ++r) {
                const double m = J[r][c] / J[c][c];
                for (int k = c; k < 5; ++k) J[r][k] -= m * J[c][k];
            }
        }
        for (int r = 3; r >= 0; --r) {
            double v = J[r][4];
            for (int k = r + 1; k < 4; ++k) v -= J[r][k] * d[k];
            d[r] = v / J[r][r];
        }
        return true;
    }

    bool newton(double iinSrc, bool dc, int maxIt) {
        const double gin = dc ? 0.0 : gin_;
        double geq, ieq;
        if (dc) { geq = 1.0 / (rdeg_ + rbot_); ieq = 0.0; }
        else { geq = geq_; ieq = (1.0 / rdeg_) * gc_ * vW_ / gtot_; }
        const Bjt& q1 = cfg_.q1;
        const Bjt& q2 = cfg_.q2;
        const double vlim = dc ? 0.1 : 0.12;
        vcrit1_ = q1.nVt * std::log(q1.nVt / (1.41421356 * q1.Is));
        vcrit2_ = q2.nVt * std::log(q2.nVt / (1.41421356 * q2.Is));
        for (int it = 0; it < maxIt; ++it) {
            ++iters_;
            double J[4][5], F[4];
            assemble(geq, ieq, gin, iinSrc, J, F);
            double d[4];
            if (!solve4(J, d)) return false;
            // SPICE-style limiting: only a junction that is being pushed further forward is held back
            // (reverse-biased moves are harmless, and a collector may swing several volts in one step)
            double alpha = 1.0;
            {
                const double vo[4] = {B1_, B1_ - B2_, B2_ - E2_, B2_ - C2_};
                const double dj[4] = {d[0], d[0] - d[1], d[1] - d[3], d[1] - d[2]};
                for (int j = 0; j < 4; ++j) {
                    if (dj[j] > vlim) {
                        const double cap = std::max(vo[j] + vlim, j < 2 ? vcrit1_ : vcrit2_);
                        if (vo[j] + dj[j] > cap) alpha = std::min(alpha, (cap - vo[j]) / dj[j]);
                    }
                }
            }
            B1_ += alpha * d[0]; B2_ += alpha * d[1]; C2_ += alpha * d[2]; E2_ += alpha * d[3];
            const double dm = std::max(std::max(std::fabs(d[0]), std::fabs(d[1])), std::max(std::fabs(d[2]), std::fabs(d[3])));
            lastDm_ = dm; lastAlpha_ = alpha;
            if (alpha == 1.0 && dm < (dc ? 1e-10 : 2e-7)) return true;
        }
        return false;
    }

    CircuitCfg cfg_;
    double h_ = 1.0 / 176400.0;
    double rdeg_ = 1.0, rbot_ = 1.0, gin_ = 0.0, gc_ = 0.0, gtot_ = 1.0, geq_ = 0.0;
    double B1_ = 0.25, B2_ = 0.5, C2_ = 4.5, E2_ = 0.4, vcIn_ = 0.0, vW_ = 0.0, quiescentC2_ = 4.5;
    int iters_ = 0;
    double lastDm_ = 0, lastAlpha_ = 1, vcrit1_ = 0.6, vcrit2_ = 0.6;
    long long fails_ = 0;
};

} // namespace verdigris
