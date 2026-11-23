#!/usr/bin/env python3
"""window.py <log> [lf_from lf_to]: fps/draw/logic per second, worst seconds and averages."""
import re, sys
f = sys.argv[1]
lo, hi = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (1, 10**9)
rows = []
for l in open(f):
    if ' fps ' in l and 'SPIKE' not in l:
        m = re.search(r'^(\S+) fps\s+([\d.]+).*draw\s+([\d.]+)/.*logic\s+([\d.]+)/.*lf (\d+)', l)
        if m and lo <= int(m.group(5)) <= hi:
            rows.append((m.group(1), float(m.group(2)), float(m.group(3)), float(m.group(4)), int(m.group(5))))
if not rows: sys.exit('no rows')
w = sorted(rows, key=lambda r: r[1])[:10]
avg = lambda xs, i: sum(x[i] for x in xs) / len(xs)
print('%s: %d s, avg fps %.1f, min %.1f, worst10 fps %.1f draw %.1f logic %.1f' % (
    f.split('/')[-1], len(rows), avg(rows, 1), w[0][1], avg(w, 1), avg(w, 2), avg(w, 3)))
