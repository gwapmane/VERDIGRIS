#!/usr/bin/env python3
"""Synthetic guitar phrase for testing: plucked strings (Karplus-Strong) at several dynamics.
usage: make_phrase.py out.wav [seconds] ; mono, 44.1 kHz, 16-bit, peaks about -10 dBFS."""
import sys, struct, wave
import numpy as np
SR = 44100
rng = np.random.default_rng(7)

def pluck(freq, dur, vel=1.0, bright=0.5):
    n = int(SR * dur)
    N = int(round(SR / freq))
    buf = (rng.uniform(-1, 1, N)) * vel
    # pick-position comb + brightness
    pp = max(1, int(N * 0.18)); buf = buf - np.roll(buf, pp) * 0.5
    out = np.zeros(n)
    g = 0.5 + 0.5 * bright
    damp = 0.996 - 0.003 * (1 - bright)
    prev = 0.0
    idx = 0
    for i in range(n):
        v = buf[idx]
        out[i] = v
        nv = damp * ((1 - 0.5 * (1 - g)) * v + 0.5 * (1 - g) * prev + 0.0)
        # simple two-point average loss filter
        avg = 0.5 * (v + buf[(idx + 1) % N])
        buf[idx] = damp * avg
        idx = (idx + 1) % N
    return out

def midi(n): return 440.0 * 2 ** ((n - 69) / 12)

events = [  # (start s, notes, dur, velocity)
    (0.0, [40], 1.2, 0.9), (1.0, [47], 0.9, 0.5), (1.8, [52], 0.9, 0.25), (2.5, [55], 0.8, 0.12),
    (3.2, [40, 47, 52], 1.6, 0.8), (5.0, [43, 50, 55], 1.6, 0.35), (6.8, [45, 52, 57], 1.4, 1.0),
    (8.4, [64], 0.5, 0.7), (8.9, [67], 0.5, 0.5), (9.4, [71], 0.5, 0.35), (9.9, [76], 1.2, 0.9),
    (11.3, [38, 45, 50, 54, 57, 62], 2.0, 0.7),
]
total = float(sys.argv[2]) if len(sys.argv) > 2 else 13.8
sig = np.zeros(int(SR * total) + SR)
for t0, notes, dur, vel in events:
    for k, nt in enumerate(notes):
        s = pluck(midi(nt), dur + 0.5, vel * (1.0 - 0.04 * k), 0.35 + 0.4 * vel)
        i = int((t0 + 0.012 * k) * SR)
        sig[i:i + len(s)] += s[: len(sig) - i]
sig = sig[: int(SR * total)]
sig *= 0.32 / np.max(np.abs(sig))
pcm = np.clip(sig * 32767, -32768, 32767).astype('<i2')
with wave.open(sys.argv[1], 'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
print('wrote', sys.argv[1], 'peak %.3f rms %.3f' % (np.max(np.abs(sig)), np.sqrt(np.mean(sig ** 2))))
