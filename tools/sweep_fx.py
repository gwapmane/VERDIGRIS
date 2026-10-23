#!/usr/bin/env python3
"""How audible is each parameter? Distance (check_presets metric) from a baseline fuzz while one thing changes."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.argv = [sys.argv[0]]
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'check_presets.py')).read().replace("\nmain()\n", "\n")
ns = {'__name__': 'cp', '__file__': os.path.join(os.path.dirname(os.path.abspath(__file__)), 'check_presets.py')}
exec(compile(src, 'cp', 'exec'), ns)
run, features, dist, level = ns['run'], ns['features'], ns['dist'], ns['level']
base = dict(fuzz=.7, type='Ge', voicing='Vin')
def f(kw, vol=0.5):
    sr, l, r, _ = run(dict(base, **kw), vol, 't'); return features(sr, l), level(l)
f0, _ = f({})
def sweep(name, key, vals, extra={}):
    print(name)
    for v in vals:
        fe, (rms, pk) = f(dict(extra, **{key: v}))
        print('  %s=%-5s dist %5.1f rms %6.1f cent %5.0f' % (key, v, dist(f0, fe), rms, fe['cent']))
if __name__ == '__main__':
    sweep('bias', 'bias', [.05, .15, .3, .5, .7, .9, 1.0])
    sweep('octAmt (octave on)', 'octAmt', [.3, .6, .9, 1.0], {'on': ['octave']})
    sweep('crushAmt (crush on)', 'crushAmt', [.15, .3, .5, .8, 1.0], {'on': ['crush']})
    sweep('starveAmt', 'starveAmt', [.3, .6, 1.0], {'on': ['starve']})
    sweep('washMix', 'washMix', [.2, .4, .7, 1.0], {'on': ['wash']})
    sweep('pickup', 'pickup', [0, .25, .5, .75, 1.0])
    sweep('tone', 'tone', [0, .25, .55, .8, 1.0])
    sweep('mids', 'mids', [0, .25, .5, .75, 1.0])
    sweep('hfe', 'hfe', [0, .25, .5, .75, 1.0])
    sweep('lowcut', 'lowcut', [0, .3, .6, 1.0])
    sweep('guitar', 'guitar', [1.0, .8, .6, .4])
    sweep('gate', 'gate', [0, .2, .5, .8])
    sweep('spread', 'spread', [0, .3, .7, 1.0])
