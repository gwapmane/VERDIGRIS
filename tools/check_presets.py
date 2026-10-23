#!/usr/bin/env python3
"""Renders every factory preset through the real engine and reports level, character and near-duplicates.
   python3 tools/check_presets.py            report
   python3 tools/check_presets.py --autovol  also set each preset's volume so they all sit at about the same loudness (writes tools/vol_auto.py)"""
import sys, os, subprocess, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze import read_wav, SCR, RENDER, PHRASE
from presets_table import P
import pack_gen
try:
    from vol_auto import VOL
except Exception:
    VOL = {}
TARGET = -21.5     # dBFS rms over the phrase
PEAK_MAX = -2.0

def run(kw, vol, name):
    args = pack_gen.render_args(dict(kw, volume=vol))
    out = SCR + '/pc.wav'
    r = subprocess.run([RENDER, PHRASE, out, 'bpm=120', 'tail=1.5'] + args, capture_output=True, text=True)
    if r.returncode: raise RuntimeError(name + ': ' + r.stderr)
    sr, d = read_wav(out)
    return sr, d[:, 0], d[:, 1], r.stdout

def level(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-18), 20 * np.log10(np.max(np.abs(x)) + 1e-12)

def features(sr, x):
    n = 4096; nb = 32
    acc = np.zeros(nb); fr = 0; cent = 0.0; tot = 0.0; cl = []; ce = []
    w = np.hanning(n)
    f = np.fft.rfftfreq(n, 1 / sr)
    edges = [80 * (125 ** (b / nb)) for b in range(nb + 1)]
    for pos in range(0, len(x) - n, n // 2):
        s = np.abs(np.fft.rfft(x[pos:pos + n] * w)) ** 2
        cent += (s * f).sum(); tot += s.sum()
        cl.append(np.log2((s * f).sum() / (s.sum() + 1e-18) + 1.0)); ce.append(10 * np.log10(s.sum() + 1e-12))
        for b in range(nb):
            acc[b] += s[(f >= edges[b]) & (f < edges[b + 1])].sum()
        fr += 1
    band = 10 * np.log10(acc / max(1, fr) + 1e-12)
    band = band - band.mean()                       # timbre only: loudness is matched separately
    step = int(sr * 0.1)
    fr100 = np.array([10 * np.log10(np.mean(x[p:p + step] ** 2) + 1e-12) for p in range(0, len(x) - step, step)])
    env = fr100[::2]
    q = np.percentile(np.maximum(fr100, -90), [10, 30, 50, 70, 90])
    cl = np.array(cl); ce = np.array(ce); cstd = float(np.std(cl[ce > ce.max() - 30])) if len(cl) else 0.0
    return dict(cstd=cstd, band=band, env=env, q=q - q[2], cent=cent / (tot + 1e-18), loud=q[2])

def dist(a, b):
    sb = np.sqrt(np.mean((a['band'] - b['band']) ** 2))
    sq = np.sqrt(np.mean((a['q'] - b['q']) ** 2))
    n = min(len(a['env']), len(b['env']))
    se = np.sqrt(np.mean((np.maximum(a['env'][:n], -80) - np.maximum(b['env'][:n], -80)) ** 2))
    return 1.4 * sb + 6.0 * abs(a['cstd'] - b['cstd']) + 0.8 * sq + 0.35 * se + 0.15 * abs(a['loud'] - b['loud'])

def main():
    auto = '--autovol' in sys.argv
    rows = []; feats = []; vols = {}; eff = {}
    for cat, name, hint, kw in P:
        vol = kw.get('volume', VOL.get(name, 0.5))
        sr, l, r, _ = run(kw, vol, name)
        rms, pk = level(l)
        if auto and 'volume' not in kw:
            for _ in range(8):   # volume is about 20 dB per 0.5 near the middle: secant steps converge fast
                tgt = min(TARGET, PEAK_MAX - (pk - rms))   # a peaky preset sits a little quieter instead of clipping
                err = tgt - rms
                if abs(err) < 0.4 and pk <= PEAK_MAX + 0.3: break
                step = np.clip(err / 40.0, -0.2, 0.2)
                vol = float(np.clip(vol + step, 0.12, 0.95))
                sr, l, r, _ = run(kw, vol, name)
                rms, pk = level(l)
            vols[name] = round(vol, 3)
        rows.append((cat, name, vol, rms, pk))
        feats.append(features(sr, l))
        if kw.get('on') or kw.get('rdepth'):
            base = dict(kw, on=[], rdepth=0)
            sr2, l2, r2, _ = run(base, vol, name)
            eff[name] = dist(feats[-1], features(sr2, l2))
    print('%-3s %-13s %-22s vol   rms    peak   cent  nearest (distance)' % ('#', 'category', 'name'))
    flagged = 0
    for i, (cat, name, vol, rms, pk) in enumerate(rows):
        ds = [(dist(feats[i], feats[j]), j) for j in range(len(rows)) if j != i]
        d, j = min(ds)
        flag = (d < 4.0) or pk > -0.3 or rms < -30 or rms > -14 or (name in eff and eff[name] < 3.5)
        flagged += flag
        print('%-3d %-13s %-22s %.2f %6.1f %6.1f %6.0f  %-20s %4.1f %s %s' % (i, cat, name, vol, rms, pk, feats[i]['cent'], rows[j][1], d, ('fx %4.1f' % eff[name]) if name in eff else '       ', '<--' if flag else ''))
    print(len(rows), 'presets,', flagged, 'flagged')
    if auto:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vol_auto.py'), 'w') as f:
            f.write('# written by check_presets.py --autovol\nVOL = ' + json.dumps({**VOL, **vols}, indent=1) + '\n')
main()
