#!/usr/bin/env python3
"""tlan.py <timeline file> [frame_to_print]: GPU busy/idle from the RX_VK_TL batch stamps, what each thread did during the GPU's idle
gaps, and one frame's event list (ms from that frame's present_in)."""
import sys, collections
path = sys.argv[1]; show = int(sys.argv[2]) if len(sys.argv) > 2 else 60
names, evs, G = {}, [], []
period = 10.0
for l in open(path):
    p = l.split()
    if l.startswith('#'): period = float(p[-1])
    elif p[0] == 'T': names[int(p[1])] = ' '.join(p[2:]) or '?'
    elif p[0] == 'E': evs.append((int(p[1]), int(p[2]), p[3], int(p[4]), int(p[5])))
    elif p[0] == 'G' and int(p[7]) == 0: G.append((int(p[1]), int(p[2]), int(p[3]) * period, int(p[4]) * period, int(p[5]), int(p[6])))
RS = ['other', 'poll', 'wait', 'slot', 'present', 'cap', 'value', 'seg']
# (the GPU clock drifts against the CPU's: a line under every (submit, start) point, the start never before the submit, fitted
# with the least total gap: slope by a grid search)
c0 = G[0][1]; g0 = G[0][2]
best = None
for k in range(-4000, 4001):
    sl = 1 + k * 2e-6
    off = min(g[2] - g0 - sl * (g[1] - c0) for g in G)
    sc = sum(g[2] - g0 - sl * (g[1] - c0) - off for g in G)
    if best is None or sc < best[0]: best = (sc, sl, off)
_, sl, off = best
print(f"GPU clock fit: slope {sl:.6f}")
cv = lambda x: c0 + (x - g0 - off) / sl
G = [(i, c, cv(a), cv(b), n, r) for i, c, a, b, n, r in G]
t0 = min(e[0] for e in evs)
ms = lambda t: (t - t0) / 1e6
# GPU busy union
iv = sorted((a, b) for _, _, a, b, _, _ in G)
busy, cur = [], None
for a, b in iv:
    if cur and a <= cur[1]: cur[1] = max(cur[1], b)
    else:
        if cur: busy.append(cur)
        cur = [a, b]
busy.append(cur)
pres = [e[0] for e in evs if e[2] == 'present_in']
lo, hi = pres[2], pres[-2]
nfr = len(pres) - 4
tot_busy = sum(max(0, min(b, hi) - max(a, lo)) for a, b in busy)
print(f"window {ms(lo):.1f}-{ms(hi):.1f} ms, {nfr} frames, {(hi-lo)/1e6/nfr:.2f} ms a frame; GPU busy {tot_busy/1e6/nfr:.2f} ms a frame "
      f"({tot_busy/(hi-lo)*100:.0f}%), batches {sum(1 for g in G if lo <= g[1] < hi)/nfr:.1f} a frame")
gaps = [(busy[i][1], busy[i+1][0]) for i in range(len(busy) - 1) if busy[i+1][0] > lo and busy[i][1] < hi]
# thread state intervals
pairs = {'dry_in': ('dry_out', 'dry'), 'cap_in': ('cap_out', 'capture'), 'submit_in': ('submit_locked', 'lockwait(submit)'), 'submit_locked': ('submit_out', 'submit'), 'replay_in': ('replay_out', 'replay'),
         'present_in': ('present_locked', 'lockwait(present)'), 'present_locked': ('present_drained', 'present-drain'),
         'present_drained': ('present_out', 'present-rest'), 'fwait_in': ('fwait_out', 'framewait'), 'swait_in': ('swait_out', 'syncwait'),
         'slot_in': ('slot_out', 'slotwait'), 'watch_in': ('watch_out', 'watch'), 'pimg_in': ('pimg_out', 'present_image')}
ivs = collections.defaultdict(list)   # tid -> (a, b, label, depth)
open_ = collections.defaultdict(list)
for t, tid, k, a, b in evs:
    for st, (en, lab) in pairs.items():
        if k == st: open_[(tid, en)].append(t)
        if k == en and open_[(tid, en)]:
            s = open_[(tid, en)].pop(); ivs[tid].append((s, t, lab))
def state(tid, t):
    best = None
    for a, b, lab in ivs[tid]:
        if a <= t < b and (best is None or a > best[0]): best = (a, lab)   # (innermost)
    return best[1] if best else '-'
# per thread time per state in window (innermost labels approximated by sampling every 0.1 ms)
step = 100000
acc = collections.defaultdict(lambda: collections.Counter())
gapacc = collections.defaultdict(lambda: collections.Counter())
gi = 0
gpu_idle_at = lambda t: any(a <= t < b for a, b in gaps)
for t in range(lo, hi, step):
    idle = gpu_idle_at(t)
    for tid in ivs:
        s = state(tid, t)
        acc[tid][s] += 1
        if idle: gapacc[tid][s] += 1
print("\nper thread, ms a frame in each state (all / while the GPU is idle):")
idle_tot = sum(min(b, hi) - max(a, lo) for a, b in gaps) / 1e6 / nfr
print(f"  GPU idle {idle_tot:.2f} ms a frame in {len(gaps)/nfr:.1f} gaps a frame")
for tid in sorted(ivs):
    row = ', '.join(f"{s} {n*step/1e6/nfr:.2f}/{gapacc[tid][s]*step/1e6/nfr:.2f}" for s, n in acc[tid].most_common(8))
    print(f"  {names.get(tid, tid):18s} {row}")
# what ends each gap: the batch that starts the GPU again
print("\ngaps by the batch that ended them (reason, flushing thread's state at the flush):")
fl = {e[3]: e for e in evs if e[2] == 'flush'}
endc = collections.Counter(); endms = collections.Counter()
for a, b in gaps:
    g = min((x for x in G if x[2] >= b - 1), key=lambda x: x[2], default=None)
    if not g: continue
    f = fl.get(g[0]); th = names.get(f[1], '?') if f else '?'
    key = (RS[g[5]], th, state(f[1], f[0]) if f else '?')
    endc[key] += 1; endms[key] += (min(b, hi) - max(a, lo)) / 1e6
for k, n in endms.most_common(12): print(f"  {n/nfr:6.2f} ms a frame, {endc[k]/nfr:.2f} gaps a frame: {k}")
gl = sorted(((b - a) / 1e6 for a, b in gaps), reverse=True)
print("largest gaps (ms):", ' '.join(f"{x:.2f}" for x in gl[:20]))
# one frame's events
if 0 <= show < len(pres) - 1:
    f0, f1 = pres[show], pres[show + 2]
    print(f"\nframes {show}..{show+1} (ms from present_in #{show}):")
    for t, tid, k, a, b in evs:
        if f0 <= t < f1 and k not in ('slot_in', 'slot_out'):
            extra = f" batch{a} {RS[b]}" if k == 'flush' else (f" v{a} q{b & 0xffffff:x}" if k.startswith(('replay', 'submit')) else (f" f{a} done{b}" if k in ('swait_in', 'fwait_in') else ''))
            if k == 'flush':
                g = next((x for x in G if x[0] == a), None)
                if g: extra += f" ncb{g[4]} gpu {(g[2]-f0)/1e6:.2f}-{(g[3]-f0)/1e6:.2f}"
            print(f"  {(t-f0)/1e6:7.2f} {names.get(tid, tid)[:16]:16s} {k}{extra}")
    print("  GPU busy:", ' '.join(f"[{(max(a,f0)-f0)/1e6:.2f}-{(min(b,f1)-f0)/1e6:.2f}]" for a, b in busy if b > f0 and a < f1))
# per batch: how long it sat with the GPU idle (start - max(submit, previous end)), by reason
print("\nbatch start delay with the GPU idle (start - max(submit, previous batch end)), by reason:")
Gs = sorted(G, key=lambda g: g[0]); prev_end = None; dl = collections.defaultdict(list)
for g in Gs:
    if prev_end is not None and lo <= g[1] < hi: dl[RS[g[5]]].append((g[2] - max(g[1], prev_end)) / 1e6)
    prev_end = max(prev_end or 0, g[3])
import statistics
for r, v in dl.items(): print(f"  {r:8s} n {len(v)/nfr:.2f}/frame median {statistics.median(v):.2f} ms, mean {sum(v)/len(v):.2f}, sum {sum(v)/nfr:.2f} ms/frame, max {max(v):.2f}")
