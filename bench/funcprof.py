#!/usr/bin/env python3
"""funcprof.py <kwfps-prof.txt> <funcs.tsv> [n]: profiler samples summed per function."""
import bisect, collections, sys
prof, funcs = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
starts, ends, names = [], [], []
for l in open(funcs):
    e, lo, hi, nm = l.rstrip('\n').split('\t')
    starts.append(int(lo, 16)); ends.append(int(hi, 16)); names.append('%s(%s)' % (nm, e))
order = sorted(range(len(starts)), key=lambda i: starts[i])
starts = [starts[i] for i in order]; ends = [ends[i] for i in order]; names = [names[i] for i in order]
def fn(a):
    i = bisect.bisect_right(starts, a) - 1
    return names[i] if i >= 0 and a <= ends[i] + 16 else '%08x?' % a
total = 0; per = collections.Counter(); mod = collections.Counter(); calls = collections.Counter()
for l in open(prof):
    if l.startswith('#'):
        total = int(l.split()[1]); continue
    tag, addr, a2, n = l.split(); n = int(n)
    if tag == 'e':
        if '+' in addr: mod[addr.split('+')[0]] += n
        else: per[fn(int(addr, 16))] += n; mod['exe'] += n
    else:
        calls[fn(int(addr, 16)) + (' < ' + fn(int(a2, 16)) if a2 != '-' else '')] += n
print('total', total)
print('by module:', ', '.join('%s %.1f%%' % (m, 100 * n / total) for m, n in mod.most_common(8)))
print('exe functions:')
for f, n in per.most_common(top): print('  %5.1f%%  %s' % (100 * n / total, f))
print('outside-exe time by exe caller:')
for f, n in calls.most_common(15): print('  %5.1f%%  %s' % (100 * n / total, f))
