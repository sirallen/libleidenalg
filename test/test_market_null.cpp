// Validation test for MarketNullModularityVertexPartition.
//
// The per-layer (market) configuration-null modularity with a single shared
// membership is mathematically identical to the sum, over layers, of stock
// RBConfigurationVertexPartition qualities (the standard multiplex objective),
// provided the single aggregate graph carries summed edge weights and the
// per-layer node strengths / totals describe each layer. This test verifies:
//
//   A. quality() of the single-graph partition equals the summed multiplex
//      quality for a fixed membership.
//   B. diff_move() equals the actual change in quality() for a real move
//      (validates the incremental per-layer admin).
//   C. A full optimise_partition() on the single graph reaches the same
//      quality as the stock 2-layer multiplex optimisation, and the
//      incrementally-tracked quality matches a from-scratch recomputation
//      (validates admin integrity through move/aggregate/refine/relabel).
//
// Build (after building the library in builds/ninja), see the command at the
// bottom of this file.

#include <igraph.h>
#include <GraphHelper.h>
#include <Optimiser.h>
#include <RBConfigurationVertexPartition.h>
#include <MarketNullModularityVertexPartition.h>

#include <cmath>
#include <cstdio>
#include <vector>
#include <map>
#include <utility>
#include <algorithm>

using std::vector;
using std::map;
using std::pair;
using std::make_pair;

static int g_failures = 0;

static void check_close(const char* name, double a, double b, double tol = 1e-9)
{
  double d = std::fabs(a - b);
  bool ok = d <= tol;
  std::printf("[%s] %s : %.10f vs %.10f (|diff|=%.3e)\n",
              ok ? "PASS" : "FAIL", name, a, b, d);
  if (!ok)
    g_failures++;
}

// Build a weighted igraph graph from an edge list (directed or undirected).
static igraph_t* make_graph_dir(size_t n, vector<size_t> const& edges_flat, bool directed)
{
  igraph_vector_int_t edges;
  igraph_vector_int_init(&edges, 0);
  for (size_t x : edges_flat)
    igraph_vector_int_push_back(&edges, (igraph_integer_t) x);
  igraph_t* g = new igraph_t();
  igraph_create(g, &edges, (igraph_integer_t) n,
                directed ? IGRAPH_DIRECTED : IGRAPH_UNDIRECTED);
  igraph_vector_int_destroy(&edges);
  return g;
}

static igraph_t* make_graph(size_t n, vector<size_t> const& edges_flat)
{
  return make_graph_dir(n, edges_flat, false);
}

// Large multi-level randomized stress test. See the invocation site for the
// rationale behind each assertion.
static void run_random_multilayer_test(bool directed, size_t n, size_t L,
    size_t edges_per_layer, int n_trials, unsigned int seed, const char* label)
{
  const double resolution = 1.0;
  unsigned int rng = seed;
  auto next = [&rng]() -> unsigned int { rng = rng * 1103515245u + 12345u; return rng; };

  // 1. Generate L random weighted layers (dedup edges within a layer, no loops).
  vector< map<pair<size_t,size_t>, double> > layer_edges(L);
  for (size_t s = 0; s < L; s++)
  {
    size_t added = 0, attempts = 0;
    while (added < edges_per_layer && attempts < edges_per_layer * 20)
    {
      attempts++;
      size_t u = next() % n, v = next() % n;
      if (u == v) continue;
      pair<size_t,size_t> key = directed ? make_pair(u, v)
                                         : make_pair(std::min(u,v), std::max(u,v));
      if (layer_edges[s].count(key)) continue;
      layer_edges[s][key] = 1.0 + (next() % 3); // weight in {1,2,3}
      added++;
    }
  }

  // 2. Build per-layer Graphs, the aggregate edge map, per-node/per-layer
  //    out/in strengths, and per-layer totals m_s.
  vector<igraph_t*> raw_layer(L, nullptr);
  vector<Graph*> G_layer(L, nullptr);
  vector<double> layer_total_weight(L, 0.0);
  map<pair<size_t,size_t>, double> agg;
  vector< map<size_t,double> > node_out(n), node_in(n); // node -> (layer -> strength)

  for (size_t s = 0; s < L; s++)
  {
    vector<size_t> flat;
    vector<double> w;
    for (map<pair<size_t,size_t>, double>::const_iterator it = layer_edges[s].begin();
         it != layer_edges[s].end(); ++it)
    {
      size_t u = it->first.first, v = it->first.second;
      double weight = it->second;
      flat.push_back(u); flat.push_back(v);
      w.push_back(weight);
      layer_total_weight[s] += weight;
      agg[it->first] += weight;
      if (directed)
      {
        node_out[u][s] += weight;
        node_in[v][s] += weight;
      }
      else
      {
        node_out[u][s] += weight; node_in[u][s] += weight;
        node_out[v][s] += weight; node_in[v][s] += weight;
      }
    }
    raw_layer[s] = make_graph_dir(n, flat, directed);
    G_layer[s] = Graph::GraphFromEdgeWeights(raw_layer[s], w);
  }

  vector<size_t> agg_flat;
  vector<double> agg_w;
  for (map<pair<size_t,size_t>, double>::const_iterator it = agg.begin(); it != agg.end(); ++it)
  {
    agg_flat.push_back(it->first.first);
    agg_flat.push_back(it->first.second);
    agg_w.push_back(it->second);
  }
  igraph_t* raw_agg = make_graph_dir(n, agg_flat, directed);
  Graph* G_agg = Graph::GraphFromEdgeWeights(raw_agg, agg_w);

  vector< vector< pair<size_t,double> > > out_list(n), in_list(n);
  for (size_t i = 0; i < n; i++)
  {
    for (map<size_t,double>::const_iterator it = node_out[i].begin(); it != node_out[i].end(); ++it)
      out_list[i].push_back(*it);
    for (map<size_t,double>::const_iterator it = node_in[i].begin(); it != node_in[i].end(); ++it)
      in_list[i].push_back(*it);
  }
  if (directed)
    G_agg->set_layer_strengths_directed(out_list, in_list, layer_total_weight);
  else
    G_agg->set_layer_strengths(out_list, layer_total_weight);

  // Invariant 1: random-membership quality equivalence.
  const double tol = 1e-6;
  bool all_ok = true;
  double max_diff = 0.0;
  for (int t = 0; t < n_trials; t++)
  {
    vector<size_t> mem(n);
    size_t n_comms = 1 + (next() % n);
    for (size_t i = 0; i < n; i++)
      mem[i] = next() % n_comms;

    MarketNullModularityVertexPartition mn(G_agg, mem, resolution);
    double a = mn.quality(resolution);
    double b = 0.0;
    for (size_t s = 0; s < L; s++)
    {
      RBConfigurationVertexPartition rb(G_layer[s], mem, resolution);
      b += rb.quality(resolution);
    }
    double d = std::fabs(a - b);
    max_diff = std::fmax(max_diff, d);
    if (d > tol)
      all_ok = false;
  }
  std::printf("[%s] %s: %d random memberships (n=%zu, L=%zu), market-null == multiplex sum (max |diff|=%.3e)\n",
              all_ok ? "PASS" : "FAIL", label, n_trials, n, L, max_diff);
  if (!all_ok)
    g_failures++;

  // Invariants 2 & 3: full optimisation exercises the multi-level collapse path.
  {
    MarketNullModularityVertexPartition mn(G_agg, resolution);
    Optimiser o;
    o.set_rng_seed(7);
    o.optimise_partition(&mn);
    double q_opt = mn.quality(resolution);
    vector<size_t> mem = mn.membership();

    MarketNullModularityVertexPartition mn_rebuilt(G_agg, mem, resolution);
    {
      char name[128];
      std::snprintf(name, sizeof(name), "%s: optimised incremental quality matches from-scratch rebuild", label);
      check_close(name, q_opt, mn_rebuilt.quality(resolution), tol);
    }

    double q_multi = 0.0;
    for (size_t s = 0; s < L; s++)
    {
      RBConfigurationVertexPartition rb(G_layer[s], mem, resolution);
      q_multi += rb.quality(resolution);
    }
    {
      char name[128];
      std::snprintf(name, sizeof(name), "%s: optimised membership scored under multiplex sum equals market-null", label);
      check_close(name, q_opt, q_multi, tol);
    }
  }

  delete G_agg;
  igraph_destroy(raw_agg); delete raw_agg;
  for (size_t s = 0; s < L; s++)
  {
    delete G_layer[s];
    igraph_destroy(raw_layer[s]); delete raw_layer[s];
  }
}

int main()
{
  const size_t n = 6;

  // Layer 0 (market 0): triangle 0-1-2 plus edge 3-4.
  vector<size_t> edges0 = {0,1, 1,2, 0,2, 3,4};
  // Layer 1 (market 1): triangle 3-4-5 plus edge 1-2.
  vector<size_t> edges1 = {3,4, 4,5, 3,5, 1,2};
  // Aggregate graph: union of both layers (edge 1-2 and 3-4 appear in both).
  vector<size_t> edges_agg = {0,1, 1,2, 0,2, 3,4, 4,5, 3,5};
  vector<double> w_agg     = {1.0, 2.0, 1.0, 2.0, 1.0, 1.0};

  // Per-node, per-layer strengths (sparse), and per-layer totals m_s.
  vector< vector< pair<size_t, double> > > node_layer_strength(n);
  node_layer_strength[0] = { {0, 2.0} };
  node_layer_strength[1] = { {0, 2.0}, {1, 1.0} };
  node_layer_strength[2] = { {0, 2.0}, {1, 1.0} };
  node_layer_strength[3] = { {0, 1.0}, {1, 2.0} };
  node_layer_strength[4] = { {0, 1.0}, {1, 2.0} };
  node_layer_strength[5] = { {1, 2.0} };
  vector<double> layer_total_weight = {4.0, 4.0};

  igraph_t* g_agg = make_graph(n, edges_agg);
  igraph_t* g0 = make_graph(n, edges0);
  igraph_t* g1 = make_graph(n, edges1);

  Graph* G_agg = Graph::GraphFromEdgeWeights(g_agg, w_agg);
  G_agg->set_layer_strengths(node_layer_strength, layer_total_weight);

  vector<double> w0(4, 1.0), w1(4, 1.0);
  Graph* G0 = Graph::GraphFromEdgeWeights(g0, w0);
  Graph* G1 = Graph::GraphFromEdgeWeights(g1, w1);

  const double resolution = 1.0;

  // ---- Test A: quality for a fixed membership ----
  vector<size_t> fixed_membership = {0, 0, 0, 1, 1, 1};

  MarketNullModularityVertexPartition mn_fixed(G_agg, fixed_membership, resolution);
  RBConfigurationVertexPartition rb0_fixed(G0, fixed_membership, resolution);
  RBConfigurationVertexPartition rb1_fixed(G1, fixed_membership, resolution);

  double q_market = mn_fixed.quality(resolution);
  double q_multiplex = rb0_fixed.quality(resolution) + rb1_fixed.quality(resolution);
  check_close("A: quality(fixed) market-null vs multiplex sum", q_market, q_multiplex);
  check_close("A: quality(fixed) equals hand-computed value", q_market, 6.0);

  // ---- Test B: diff_move equals actual quality change ----
  {
    MarketNullModularityVertexPartition mn(G_agg, resolution); // singletons
    size_t v = 1, target = mn.membership(0);
    double q_before = mn.quality(resolution);
    double predicted = mn.diff_move(v, target);
    mn.move_node(v, target);
    double q_after = mn.quality(resolution);
    check_close("B: diff_move predicts quality change", predicted, q_after - q_before);
  }

  // ---- Test C: full optimisation ----
  {
    MarketNullModularityVertexPartition mn(G_agg, resolution);
    Optimiser o_single;
    o_single.set_rng_seed(42);
    o_single.optimise_partition(&mn);
    double q_opt_single = mn.quality(resolution);

    // Recompute from scratch with the same membership to check admin integrity.
    MarketNullModularityVertexPartition mn_rebuilt(G_agg, mn.membership(), resolution);
    check_close("C: incremental quality matches from-scratch rebuild",
                q_opt_single, mn_rebuilt.quality(resolution));

    // Stock 2-layer multiplex optimisation of the same objective.
    RBConfigurationVertexPartition rb0(G0, resolution);
    RBConfigurationVertexPartition rb1(G1, resolution);
    vector<MutableVertexPartition*> parts = {&rb0, &rb1};
    vector<double> layer_weights = {1.0, 1.0};
    vector<bool> is_fixed(n, false);
    Optimiser o_multi;
    o_multi.set_rng_seed(42);
    o_multi.optimise_partition(parts, layer_weights, is_fixed);
    double q_opt_multi = rb0.quality(resolution) + rb1.quality(resolution);

    check_close("C: optimised market-null vs optimised multiplex", q_opt_single, q_opt_multi);
    check_close("C: optimised quality reaches known optimum", q_opt_single, 6.0);
  }

  // ---- Test D: randomized membership quality equivalence ----
  // Independent of the optimiser: for many random community assignments, the
  // single-graph market-null quality must equal the summed multiplex quality.
  {
    unsigned int seed = 12345u;
    bool all_ok = true;
    double max_diff = 0.0;
    for (int trial = 0; trial < 500; trial++)
    {
      vector<size_t> mem(n);
      size_t n_comms = 1 + (seed % n);
      for (size_t i = 0; i < n; i++)
      {
        seed = seed * 1103515245u + 12345u; // LCG
        mem[i] = (seed >> 16) % n_comms;
      }
      MarketNullModularityVertexPartition mn(G_agg, mem, resolution);
      RBConfigurationVertexPartition rb0(G0, mem, resolution);
      RBConfigurationVertexPartition rb1(G1, mem, resolution);
      double a = mn.quality(resolution);
      double b = rb0.quality(resolution) + rb1.quality(resolution);
      max_diff = std::fmax(max_diff, std::fabs(a - b));
      if (std::fabs(a - b) > 1e-9)
        all_ok = false;
    }
    std::printf("[%s] D: 500 random memberships, market-null == multiplex sum (max |diff|=%.3e)\n",
                all_ok ? "PASS" : "FAIL", max_diff);
    if (!all_ok)
      g_failures++;
  }

  delete G_agg; delete G0; delete G1;
  igraph_destroy(g_agg); delete g_agg;
  igraph_destroy(g0); delete g0;
  igraph_destroy(g1); delete g1;

  // ======================================================================
  // Directed (Leicht-Newman) market-null modularity.
  //
  // Same checks as above but on directed layers, validated against the stock
  // directed RBConfigurationVertexPartition summed over layers. The directed
  // null is sum_s k_i^out,(s) k_j^in,(s) / m_s.
  // ======================================================================
  {
    // Layer 0: directed triangle 0->1->2->0, plus 3->4.
    vector<size_t> d_edges0 = {0,1, 1,2, 2,0, 3,4};
    // Layer 1: directed triangle 3->4->5->3, plus 1->2.
    vector<size_t> d_edges1 = {3,4, 4,5, 5,3, 1,2};
    // Aggregate: union with summed weights (1->2 and 3->4 appear in both).
    vector<size_t> d_edges_agg = {0,1, 1,2, 2,0, 3,4, 4,5, 5,3};
    vector<double> d_w_agg     = {1.0, 2.0, 1.0, 2.0, 1.0, 1.0};

    vector< vector< pair<size_t, double> > > out_strength(n), in_strength(n);
    // out-strengths per (node, layer)
    out_strength[0] = { {0, 1.0} };
    out_strength[1] = { {0, 1.0}, {1, 1.0} };
    out_strength[2] = { {0, 1.0} };
    out_strength[3] = { {0, 1.0}, {1, 1.0} };
    out_strength[4] = { {1, 1.0} };
    out_strength[5] = { {1, 1.0} };
    // in-strengths per (node, layer)
    in_strength[0] = { {0, 1.0} };
    in_strength[1] = { {0, 1.0} };
    in_strength[2] = { {0, 1.0}, {1, 1.0} };
    in_strength[3] = { {1, 1.0} };
    in_strength[4] = { {0, 1.0}, {1, 1.0} };
    in_strength[5] = { {1, 1.0} };
    vector<double> d_layer_total_weight = {4.0, 4.0};

    igraph_t* dg_agg = make_graph_dir(n, d_edges_agg, true);
    igraph_t* dg0 = make_graph_dir(n, d_edges0, true);
    igraph_t* dg1 = make_graph_dir(n, d_edges1, true);

    Graph* DG_agg = Graph::GraphFromEdgeWeights(dg_agg, d_w_agg);
    DG_agg->set_layer_strengths_directed(out_strength, in_strength, d_layer_total_weight);
    vector<double> dw0(4, 1.0), dw1(4, 1.0);
    Graph* DG0 = Graph::GraphFromEdgeWeights(dg0, dw0);
    Graph* DG1 = Graph::GraphFromEdgeWeights(dg1, dw1);

    // ---- Test E: quality for fixed membership (directed) ----
    vector<size_t> fixed_membership = {0, 0, 0, 1, 1, 1};
    MarketNullModularityVertexPartition mn_fixed(DG_agg, fixed_membership, resolution);
    RBConfigurationVertexPartition rb0_fixed(DG0, fixed_membership, resolution);
    RBConfigurationVertexPartition rb1_fixed(DG1, fixed_membership, resolution);
    check_close("E: directed quality(fixed) market-null vs multiplex sum",
                mn_fixed.quality(resolution),
                rb0_fixed.quality(resolution) + rb1_fixed.quality(resolution));

    // ---- Test F: diff_move equals actual quality change (directed) ----
    {
      MarketNullModularityVertexPartition mn(DG_agg, resolution);
      size_t v = 1, target = mn.membership(0);
      double q_before = mn.quality(resolution);
      double predicted = mn.diff_move(v, target);
      mn.move_node(v, target);
      double q_after = mn.quality(resolution);
      check_close("F: directed diff_move predicts quality change",
                  predicted, q_after - q_before);
    }

    // ---- Test G: full optimisation (directed) ----
    {
      MarketNullModularityVertexPartition mn(DG_agg, resolution);
      Optimiser o_single;
      o_single.set_rng_seed(42);
      o_single.optimise_partition(&mn);
      double q_opt_single = mn.quality(resolution);

      MarketNullModularityVertexPartition mn_rebuilt(DG_agg, mn.membership(), resolution);
      check_close("G: directed incremental quality matches from-scratch rebuild",
                  q_opt_single, mn_rebuilt.quality(resolution));

      RBConfigurationVertexPartition rb0(DG0, resolution);
      RBConfigurationVertexPartition rb1(DG1, resolution);
      vector<MutableVertexPartition*> parts = {&rb0, &rb1};
      vector<double> layer_weights = {1.0, 1.0};
      vector<bool> is_fixed(n, false);
      Optimiser o_multi;
      o_multi.set_rng_seed(42);
      o_multi.optimise_partition(parts, layer_weights, is_fixed);
      double q_opt_multi = rb0.quality(resolution) + rb1.quality(resolution);
      check_close("G: directed optimised market-null vs optimised multiplex",
                  q_opt_single, q_opt_multi);
    }

    // ---- Test H: randomized membership quality equivalence (directed) ----
    {
      unsigned int seed = 67890u;
      bool all_ok = true;
      double max_diff = 0.0;
      for (int trial = 0; trial < 500; trial++)
      {
        vector<size_t> mem(n);
        size_t n_comms = 1 + (seed % n);
        for (size_t i = 0; i < n; i++)
        {
          seed = seed * 1103515245u + 12345u;
          mem[i] = (seed >> 16) % n_comms;
        }
        MarketNullModularityVertexPartition mn(DG_agg, mem, resolution);
        RBConfigurationVertexPartition rb0(DG0, mem, resolution);
        RBConfigurationVertexPartition rb1(DG1, mem, resolution);
        double a = mn.quality(resolution);
        double b = rb0.quality(resolution) + rb1.quality(resolution);
        max_diff = std::fmax(max_diff, std::fabs(a - b));
        if (std::fabs(a - b) > 1e-9)
          all_ok = false;
      }
      std::printf("[%s] H: 500 random memberships (directed), market-null == multiplex sum (max |diff|=%.3e)\n",
                  all_ok ? "PASS" : "FAIL", max_diff);
      if (!all_ok)
        g_failures++;
    }

    delete DG_agg; delete DG0; delete DG1;
    igraph_destroy(dg_agg); delete dg_agg;
    igraph_destroy(dg0); delete dg0;
    igraph_destroy(dg1); delete dg1;
  }

  // ======================================================================
  // Large multi-level randomized stress test (Tests I/J).
  //
  // Builds many random weighted layers on a larger node set, forms the
  // aggregate graph (summed weights) plus per-layer node strengths, and checks
  // the invariants that must hold regardless of the optimiser's stochastic path:
  //
  //   1. For many random memberships, the single-graph market-null quality
  //      equals the stock per-layer RBConfiguration quality summed over layers.
  //   2. After a *full* Leiden optimisation of the single market-null graph,
  //      the incrementally-tracked quality equals a from-scratch rebuild with
  //      the same membership (admin integrity through move/collapse/refine/
  //      relabel at multiple aggregation levels).
  //   3. The membership found by the market-null optimiser, scored under the
  //      stock multiplex sum, equals the market-null optimised quality. This
  //      confirms the optimiser maximised the intended multiplex objective
  //      through every collapse level. (We do NOT compare against the multiplex
  //      optimiser's own optimum: distinct Leiden runs may reach different
  //      local optima on a large graph.)
  //
  // Runs for both undirected and directed layers.
  run_random_multilayer_test(/*directed=*/false, /*n=*/80, /*L=*/8,
                             /*edges_per_layer=*/200, /*n_trials=*/200,
                             /*seed=*/2024u, "I (undirected)");
  run_random_multilayer_test(/*directed=*/true, /*n=*/80, /*L=*/8,
                             /*edges_per_layer=*/200, /*n_trials=*/200,
                             /*seed=*/4048u, "J (directed)");

  if (g_failures == 0)
    std::printf("\nAll checks passed.\n");
  else
    std::printf("\n%d check(s) FAILED.\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
