#!/usr/bin/env python3
"""analysis helpers: render with the C++ tool and measure the result."""
import subprocess, struct, sys, os, wave
import numpy as np
SCR = os.environ.get('SCR', '/tmp/claude-0/-home-claude/4fb16f49-6531-5466-b05e-c1e8f38d1e88/scratchpad')
RENDER = SCR + '/render'
PHRASE = SCR + '/phrase.wav'

def read_wav(path):
    with open(path, 'rb') as f: b = f.read()
    i = 12; sr = 44100; ch = 2; data = None
    while i < len(b):
        cid = b[i:i+4]; sz = struct.unpack('<I', b[i+4:i+8])[0]
        if cid == b'fmt ': ch = struct.unpack('<H', b[i+10:i+12])[0]; sr = struct.unpack('<I', b[i+12:i+16])[0]
        if cid == b'data': data = np.frombuffer(b[i+8:i+8+sz], dtype='<f4').reshape(-1, ch); break
        i += 8 + sz
    return sr, data

def render(args, inp=PHRASE, out=None):
    out = out or (SCR + '/tmp_%d.wav' % (hash(tuple(args)) & 0xffffff))
    r = subprocess.run([RENDER, inp, out] + [str(a) for a in args], capture_output=True, text=True)
    if r.returncode: raise RuntimeError(r.stderr)
    return read_wav(out), r.stdout

def stats(sr, x):
    m = x[:, 0]
    n = len(m)
    act = np.abs(m) > 1e-4
    rms = np.sqrt(np.mean(m ** 2) + 1e-18)
    pk = np.max(np.abs(m))
    spec = np.abs(np.fft.rfft(m * np.hanning(n))) ** 2
    fr = np.fft.rfftfreq(n, 1 / sr)
    tot = spec.sum() + 1e-18
    cent = (spec * fr).sum() / tot
    def band(a, b): return 10 * np.log10(spec[(fr >= a) & (fr < b)].sum() / tot + 1e-12)
    return dict(rms=20 * np.log10(rms), peak=20 * np.log10(pk + 1e-12), crest=20 * np.log10(pk / rms), cent=cent,
                lo=band(20, 250), mid=band(250, 2500), hi=band(2500, 20000))

if __name__ == '__main__':
    for fz in (0.0, 0.25, 0.5, 0.75, 1.0):
        (sr, x), o = render(['fuzz=%g' % fz, 'volume=0.5'])
        s = stats(sr, x); print('fuzz %.2f  rms %6.1f  peak %6.1f  crest %4.1f  centroid %5.0f  lo %5.1f mid %5.1f hi %5.1f' % (fz, s['rms'], s['peak'], s['crest'], s['cent'], s['lo'], s['mid'], s['hi']))
