"""Local validation of the marketnull pybind11 module.

Reproduces the toy graphs from test/test_market_null.cpp (whose market-null
qualities for the fixed membership {0,0,0,1,1,1} are known exactly: 6.0
undirected, 3.0 directed) and checks:

  1. market_null_quality(fixed) matches the known C++ value (binding plumbs the
     edge weights + per-layer strengths/totals through correctly).
  2. optimise_market_null returns a quality consistent with re-scoring its own
     returned membership via market_null_quality, and at least as good as the
     fixed membership.

Run from the repo root after building the module into marketnull/:
    python marketnull/validate.py
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marketnull  # noqa: E402

failures = 0


def check_close(name, a, b, tol=1e-9):
    global failures
    ok = abs(a - b) <= tol
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {a:.10f} vs {b:.10f} (|diff|={abs(a-b):.3e})")
    if not ok:
        failures += 1


def check(name, cond):
    global failures
    print(f"[{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        failures += 1


N = 6
RES = 1.0
FIXED = np.array([0, 0, 0, 1, 1, 1], dtype=np.int64)

# ---- Undirected toy (aggregate of two layers) ----
u_src = np.array([0, 1, 0, 3, 4, 3], dtype=np.int64)
u_dst = np.array([1, 2, 2, 4, 5, 5], dtype=np.int64)
u_w = np.array([1.0, 2.0, 1.0, 2.0, 1.0, 1.0])
# (node, layer, strength) sparse triples; in-strength unused for undirected.
u_node = np.array([0, 1, 1, 2, 2, 3, 3, 4, 4, 5], dtype=np.int64)
u_layer = np.array([0, 0, 1, 0, 1, 0, 1, 0, 1, 1], dtype=np.int64)
u_out = np.array([2.0, 2.0, 1.0, 2.0, 1.0, 1.0, 2.0, 1.0, 2.0, 2.0])
u_in = np.array([], dtype=np.float64)
u_totals = np.array([4.0, 4.0])

q_fixed_u = marketnull.market_null_quality(
    N, u_src, u_dst, u_w, u_node, u_layer, u_out, u_in, u_totals,
    FIXED, resolution=RES, directed=False,
)
check_close("undirected market_null_quality(fixed) == 6.0", q_fixed_u, 6.0)

mem_u, q_opt_u = marketnull.optimise_market_null(
    N, u_src, u_dst, u_w, u_node, u_layer, u_out, u_in, u_totals,
    resolution=RES, seed=42, directed=False,
)
q_rescored_u = marketnull.market_null_quality(
    N, u_src, u_dst, u_w, u_node, u_layer, u_out, u_in, u_totals,
    np.asarray(mem_u, dtype=np.int64), resolution=RES, directed=False,
)
check_close("undirected optimise quality == re-scored membership", q_opt_u, q_rescored_u)
check("undirected optimised quality >= fixed-membership quality", q_opt_u >= q_fixed_u - 1e-9)

# ---- Directed toy (Leicht-Newman). Rows are the union of out/in participation
# per (node, layer); out or in is 0 where the node has no edge of that direction.
d_src = np.array([0, 1, 2, 3, 4, 5], dtype=np.int64)
d_dst = np.array([1, 2, 0, 4, 5, 3], dtype=np.int64)
d_w = np.array([1.0, 2.0, 1.0, 2.0, 1.0, 1.0])
d_node = np.array([0, 1, 1, 2, 2, 3, 3, 4, 4, 5], dtype=np.int64)
d_layer = np.array([0, 0, 1, 0, 1, 0, 1, 0, 1, 1], dtype=np.int64)
d_out = np.array([1.0, 1.0, 1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0])
d_in = np.array([1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0, 1.0, 1.0])
d_totals = np.array([4.0, 4.0])

q_fixed_d = marketnull.market_null_quality(
    N, d_src, d_dst, d_w, d_node, d_layer, d_out, d_in, d_totals,
    FIXED, resolution=RES, directed=True,
)
check_close("directed market_null_quality(fixed) == 3.0", q_fixed_d, 3.0)

mem_d, q_opt_d = marketnull.optimise_market_null(
    N, d_src, d_dst, d_w, d_node, d_layer, d_out, d_in, d_totals,
    resolution=RES, seed=42, directed=True,
)
q_rescored_d = marketnull.market_null_quality(
    N, d_src, d_dst, d_w, d_node, d_layer, d_out, d_in, d_totals,
    np.asarray(mem_d, dtype=np.int64), resolution=RES, directed=True,
)
check_close("directed optimise quality == re-scored membership", q_opt_d, q_rescored_d)
check("directed optimised quality >= fixed-membership quality", q_opt_d >= q_fixed_d - 1e-9)

# ---- Fixed-node (hub freezing) behaviour on the undirected toy ----
# Freezing ALL nodes must leave them in their initial singletons (verifies the
# optimiser never moves a fixed node), and the quality must equal the singleton
# quality.
identity = np.arange(N, dtype=np.int64)
q_singletons = marketnull.market_null_quality(
    N, u_src, u_dst, u_w, u_node, u_layer, u_out, u_in, u_totals,
    identity, resolution=RES, directed=False,
)
mem_frozen, q_frozen = marketnull.optimise_market_null(
    N, u_src, u_dst, u_w, u_node, u_layer, u_out, u_in, u_totals,
    resolution=RES, seed=42, directed=False,
    fixed_nodes=identity, reassign_fixed_at_end=False,
)
check("freeze-all keeps every node in its singleton", list(mem_frozen) == list(range(N)))
check_close("freeze-all quality == singleton quality", q_frozen, q_singletons)

# Freezing all then reassigning (single final sweep, all unfrozen) must recover
# the optimum on this small graph.
mem_re, q_re = marketnull.optimise_market_null(
    N, u_src, u_dst, u_w, u_node, u_layer, u_out, u_in, u_totals,
    resolution=RES, seed=42, directed=False,
    fixed_nodes=identity, reassign_fixed_at_end=True,
)
check("freeze-all+reassign improves over singletons", q_re > q_singletons + 1e-9)
check_close("freeze-all+reassign recovers optimum 6.0", q_re, 6.0)

print(f"\nmembership (undirected) = {list(mem_u)}")
print(f"membership (directed)   = {list(mem_d)}")
print("\nAll checks passed." if failures == 0 else f"\n{failures} check(s) FAILED.")
sys.exit(0 if failures == 0 else 1)
