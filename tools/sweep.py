#!/usr/bin/env python3
import sys, wave, numpy as np
sys.path.insert(0, 'tools')
import analyze as A
SR = 44100
def tone_wav(path, f=220.0, amp=0.1, dur=1.5):
    t = np.arange(int(SR * dur)) / SR
    x = amp * np.sin(2 * np.pi * f * t) * np.minimum(1, t / 0.05)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes((x * 32767).astype('<i2').tobytes())
def thd(x, f0=220.0):
    n = len(x); seg = x[int(0.8 * SR):int(1.4 * SR), 0]
    t = np.arange(len(seg)) / SR
    amps = []
    for k in range(1, 12):
        c = np.sum(seg * np.cos(2 * np.pi * k * f0 * t)); s = np.sum(seg * np.sin(2 * np.pi * k * f0 * t))
        amps.append(2 * np.hypot(c, s) / len(seg))
    a = np.array(amps)
    return 20 * np.log10(a[0] + 1e-12), 100 * np.sqrt(np.sum(a[1:] ** 2)) / (a[0] + 1e-12), np.sqrt(np.mean(seg ** 2))
def run(extra, fz_list=(0, .125, .25, .375, .5, .625, .75, .875, 1.0), levels=(-34, -24, -14, -6)):
    print('fuzz   ' + '   '.join('in %4d dB: out/THD%%' % l for l in levels))
    for fz in fz_list:
        row = []
        for l in levels:
            p = A.SCR + '/tone.wav'; tone_wav(p, amp=10 ** (l / 20))
            (sr, x), o = A.render(['fuzz=%g' % fz, 'volume=0.5', 'tail=0.2'] + extra, inp=p)
            fund, th, rms = thd(x)
            row.append('%6.1f/%5.0f' % (fund, th))
        print('%.3f  ' % fz + '      '.join(row))
if __name__ == '__main__':
    run(sys.argv[1:])
