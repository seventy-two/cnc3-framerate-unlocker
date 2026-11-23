"""posjumps.py <log>...: count jumps in POS traces (one tracked unit's drawn
position per client frame). A jump is a frame step more than 3x the median of
its neighbours, or a step against the direction of travel."""
import re, sys, statistics
for path in sys.argv[1:]:
    seqs, cur = [], []
    for l in open(path, errors='replace'):
        if 'POS track' in l or 'POS lost' in l:
            if cur: seqs.append(cur)
            cur = []
        m = re.search(r'POS cf (\d+) .*?\| ([-\d.]+) ([-\d.]+) ', l)
        if m: cur.append((int(m.group(1)), float(m.group(2)), float(m.group(3))))
    if cur: seqs.append(cur)
    steps = jumps = back = 0
    for s in seqs:
        d = [(b[1]-a[1], b[2]-a[2]) for a, b in zip(s, s[1:]) if b[0] == a[0] + 1]
        mag = [(x*x+y*y) ** .5 for x, y in d]
        for i in range(len(d)):
            w = mag[max(0, i-8):i] + mag[i+1:i+9]
            if len(w) < 6: continue
            med = statistics.median(w)
            if med < 0.02: continue  # standing still
            steps += 1
            if mag[i] > 3 * med: jumps += 1
            px = sum(x for x, _ in d[max(0, i-8):i]); py = sum(y for _, y in d[max(0, i-8):i])
            if d[i][0]*px + d[i][1]*py < 0 and mag[i] > 0.5 * med: back += 1
    print(f"{path}: {len(seqs)} units, {steps} moving frames, {jumps} jumps (>3x median step), {back} backward steps")
