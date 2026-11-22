"""Summarise a kwfps.log benchmark run: only seconds where the replay was
playing (logic frame > 0) count."""
import re, sys

LINE = re.compile(r'fps\s+([\d.]+) \| frame avg\s+([\d.]+) max\s+([\d.]+) \| update\s+([\d.]+)/\s*([\d.]+) \| client\s+([\d.]+)/\s*([\d.]+) '
                  r'\(gameclient\s+([\d.]+)/\s*([\d.]+), draw\s+([\d.]+)/\s*([\d.]+)\) \| logic\s+([\d.]+)/\s*([\d.]+).*spikes (\d+) \| lf (\d+)')
SPIKE = re.compile(r'SPIKE gap ([\d.]+) ms \| this frame: update ([\d.]+) client ([\d.]+) draw ([\d.]+) logic ([\d.]+).*lf (\d+)')

def summarise(path):
    secs, spikes = [], []
    for raw in open(path, encoding='latin1'):
        m = LINE.search(raw)
        if m and int(m.group(15)) > 0:
            secs.append([float(x) for x in m.groups()])
        s = SPIKE.search(raw)
        if s and int(s.group(6)) > 0:
            spikes.append([float(x) for x in s.groups()])
    n = len(secs)
    avg = lambda i: sum(r[i] for r in secs) / n
    worst = lambda i: max(r[i] for r in secs)
    fps = sorted(r[0] for r in secs)
    print(f'{path}: {n} s of replay')
    print(f'  fps avg {avg(0):.1f}, worst second {fps[0]:.1f}, 5th percentile {fps[max(0, n // 20)]:.1f}, seconds under 85: {sum(1 for f in fps if f < 85)}')
    print(f'  frame work (ms) avg/worst: update {avg(3):.2f}/{worst(4):.1f}  draw {avg(9):.2f}/{worst(10):.1f}  logic {avg(11):.2f}/{worst(12):.1f}')
    print(f'  spikes (>SpikeMs gap): {len(spikes)}', end='')
    if spikes:
        d = sum(1 for s in spikes if s[3] >= s[4]); print(f', draw-led {d}, logic-led {len(spikes) - d}, worst gap {max(s[0] for s in spikes):.0f} ms')
    else:
        print()

for p in sys.argv[1:]:
    summarise(p)
