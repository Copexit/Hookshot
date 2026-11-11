#!/usr/bin/env python3
"""livestat.py <game log> [ref=base]: the live A/B windows of one launch (RX_VK_LIVE_CFGS; log lines "vk: live cfg ...").
Per config: median fps (presents / s) and draws per window; for every config but the reference, the paired per-round
difference vs the reference in %, with a sign-flip permutation p and a Wilcoxon signed-rank p (* = both < 0.05).
Put a second copy of the reference in the configs file (e.g. "base2:") as a sanity row: it should come out near 0, not significant.
Needs numpy and scipy."""
import re, sys
import numpy as np
from scipy.stats import wilcoxon

log = sys.argv[1]; ref = sys.argv[2] if len(sys.argv) > 2 else "base"
w = {}
for l in open(log, errors="replace"):
    m = re.search(r"vk: live cfg (\S+): (\d+) presents in ([0-9.]+) ms, draws (\d+) \(round (\d+)\)", l)
    if m: w.setdefault(m.group(1), {})[int(m.group(5))] = (int(m.group(2)) / float(m.group(3)) * 1000, int(m.group(4)))
rng = np.random.default_rng(1)
def perm_p(d):
    d = np.asarray(d); flips = rng.choice([-1.0, 1.0], size=(20000, d.size))
    return (np.abs((flips * d).mean(axis=1)) >= abs(d.mean())).mean()
print(f"{log}: live windows, reference {ref}")
print(f"{'config':14s} {'fps':>6s} {'draws':>6s} {'dfps%':>7s} {'p perm/wilc':>13s}  n")
for name, rs in w.items():
    f = [v[0] for v in rs.values()]; d = [v[1] for v in rs.values()]
    row = f"{name:14s} {np.median(f):6.2f} {np.median(d):6.0f}"
    if name != ref and ref in w:
        ds = [(rs[r][0] - w[ref][r][0]) / w[ref][r][0] * 100 for r in rs if r in w[ref]]
        p = perm_p(ds); pw = wilcoxon(ds).pvalue if len(ds) > 5 else 1.0
        row += f" {np.median(ds):+6.1f}% {p:6.3f}/{pw:5.3f}{'*' if max(p, pw) < 0.05 else ' '}"
    print(row + f"  {len(f)}")
