// Thin pybind11 binding around libleidenalg's MarketNullModularityVertexPartition.
//
// It exposes exactly what the trade-graph ETL needs: build the aggregate graph
// from a precomputed edge list, attach per-(node, layer) strengths and per-layer
// totals (computed upstream in Spark), then either optimise a single shared
// community assignment under the market-aggregated configuration null, or score
// a fixed membership under that same objective.
//
// The heavy graph preparation (aggregation, densifying wallet/market ids,
// per-layer strengths) lives in Spark; this module only consumes compact arrays.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <igraph/igraph.h>
#include <GraphHelper.h>
#include <Optimiser.h>
#include <MarketNullModularityVertexPartition.h>

#include <vector>
#include <utility>
#include <stdexcept>

namespace py = pybind11;
using std::vector;
using std::pair;

namespace {

using IntArray = py::array_t<int64_t, py::array::c_style | py::array::forcecast>;
using DblArray = py::array_t<double,  py::array::c_style | py::array::forcecast>;

// Build the aggregate igraph in `g` (caller owns it and must igraph_destroy it
// only after deleting the returned Graph, since Graph keeps a borrowed pointer).
// Attaches per-(node, layer) out/in strengths and per-layer totals.
Graph* build_market_graph(
    igraph_t* g,
    size_t n_vertices,
    IntArray const& edge_src, IntArray const& edge_dst, DblArray const& edge_weight,
    IntArray const& strength_node, IntArray const& strength_layer,
    DblArray const& strength_out, DblArray const& strength_in,
    DblArray const& layer_total_weight,
    bool directed)
{
  auto es = edge_src.unchecked<1>();
  auto ed = edge_dst.unchecked<1>();
  auto ew = edge_weight.unchecked<1>();
  const py::ssize_t E = es.shape(0);
  if (ed.shape(0) != E || ew.shape(0) != E)
    throw std::invalid_argument("edge_src, edge_dst, edge_weight must have equal length.");

  auto sn = strength_node.unchecked<1>();
  auto sl = strength_layer.unchecked<1>();
  auto so = strength_out.unchecked<1>();
  const py::ssize_t S = sn.shape(0);
  if (sl.shape(0) != S || so.shape(0) != S)
    throw std::invalid_argument("strength_node, strength_layer, strength_out must have equal length.");
  auto si = strength_in.unchecked<1>();
  if (directed && si.shape(0) != S)
    throw std::invalid_argument("strength_in must match the strength arrays' length when directed=True.");

  auto lt = layer_total_weight.unchecked<1>();
  const py::ssize_t L = lt.shape(0);

  igraph_vector_int_t edges;
  igraph_vector_int_init(&edges, 2 * E);
  for (py::ssize_t e = 0; e < E; e++)
  {
    VECTOR(edges)[2 * e]     = (igraph_integer_t) es(e);
    VECTOR(edges)[2 * e + 1] = (igraph_integer_t) ed(e);
  }
  igraph_create(g, &edges, (igraph_integer_t) n_vertices,
                directed ? IGRAPH_DIRECTED : IGRAPH_UNDIRECTED);
  igraph_vector_int_destroy(&edges);

  vector<double> weights((size_t) E);
  for (py::ssize_t e = 0; e < E; e++)
    weights[(size_t) e] = ew(e);

  Graph* G = Graph::GraphFromEdgeWeights(g, weights);

  vector< vector< pair<size_t, double> > > out_list(n_vertices);
  vector< vector< pair<size_t, double> > > in_list;
  if (directed)
    in_list.resize(n_vertices);

  for (py::ssize_t i = 0; i < S; i++)
  {
    size_t node = (size_t) sn(i);
    size_t layer = (size_t) sl(i);
    if (node >= n_vertices)
      throw std::invalid_argument("strength_node contains an id >= n_vertices.");
    out_list[node].push_back(std::make_pair(layer, so(i)));
    if (directed)
      in_list[node].push_back(std::make_pair(layer, si(i)));
  }

  vector<double> totals((size_t) L);
  for (py::ssize_t s = 0; s < L; s++)
    totals[(size_t) s] = lt(s);

  if (directed)
    G->set_layer_strengths_directed(out_list, in_list, totals);
  else
    G->set_layer_strengths(out_list, totals);

  return G;
}

py::tuple optimise_market_null(
    size_t n_vertices,
    IntArray edge_src, IntArray edge_dst, DblArray edge_weight,
    IntArray strength_node, IntArray strength_layer,
    DblArray strength_out, DblArray strength_in,
    DblArray layer_total_weight,
    double resolution, size_t seed, bool directed,
    IntArray fixed_nodes, bool reassign_fixed_at_end)
{
  igraph_t g;
  Graph* G = build_market_graph(&g, n_vertices, edge_src, edge_dst, edge_weight,
                                strength_node, strength_layer, strength_out,
                                strength_in, layer_total_weight, directed);

  // Nodes in `fixed_nodes` (e.g. high-participation "hub"/market-maker wallets)
  // are frozen in their initial singleton community: the optimiser never
  // evaluates a move FOR them, eliminating their dominant per-move cost (which
  // scales with the number of markets they trade). Other nodes may still join a
  // hub's community if that improves quality. With reassign_fixed_at_end, a
  // single final local-move sweep reassigns ONLY the hubs (everything else held
  // fixed), so the hubs' expensive moves are paid once rather than every sweep.
  auto fn = fixed_nodes.unchecked<1>();
  bool any_fixed = false;
  vector<bool> is_fixed(n_vertices, false);
  for (py::ssize_t i = 0; i < fn.shape(0); i++)
  {
    size_t node = (size_t) fn(i);
    if (node >= n_vertices)
      throw std::invalid_argument("fixed_nodes contains an id >= n_vertices.");
    is_fixed[node] = true;
    any_fixed = true;
  }

  MarketNullModularityVertexPartition partition(G, resolution);
  Optimiser o;
  o.set_rng_seed(seed);
  {
    py::gil_scoped_release release; // pure C++ work below; no Python objects touched
    o.optimise_partition(&partition, is_fixed);

    if (reassign_fixed_at_end && any_fixed)
    {
      // Hold everything except the hubs fixed and run one local-move sweep, so
      // each hub snaps into the best community given the final structure.
      vector<bool> fix_others(n_vertices, true);
      for (py::ssize_t i = 0; i < fn.shape(0); i++)
        fix_others[(size_t) fn(i)] = false;
      o.move_nodes(&partition, fix_others, o.consider_comms, false);
    }
  }
  double quality = partition.quality(resolution);
  vector<size_t> membership = partition.membership();

  delete G;
  igraph_destroy(&g);

  py::list mem;
  for (size_t m : membership)
    mem.append((py::int_)(unsigned long long) m);
  return py::make_tuple(mem, quality);
}

double market_null_quality(
    size_t n_vertices,
    IntArray edge_src, IntArray edge_dst, DblArray edge_weight,
    IntArray strength_node, IntArray strength_layer,
    DblArray strength_out, DblArray strength_in,
    DblArray layer_total_weight,
    IntArray membership,
    double resolution, bool directed)
{
  igraph_t g;
  Graph* G = build_market_graph(&g, n_vertices, edge_src, edge_dst, edge_weight,
                                strength_node, strength_layer, strength_out,
                                strength_in, layer_total_weight, directed);

  auto mb = membership.unchecked<1>();
  if (mb.shape(0) != (py::ssize_t) n_vertices)
    throw std::invalid_argument("membership length must equal n_vertices.");
  vector<size_t> mem(n_vertices);
  for (size_t v = 0; v < n_vertices; v++)
    mem[v] = (size_t) mb(v);

  MarketNullModularityVertexPartition partition(G, mem, resolution);
  double quality = partition.quality(resolution);

  delete G;
  igraph_destroy(&g);
  return quality;
}

} // namespace

PYBIND11_MODULE(marketnull, m)
{
  m.doc() = "Thin binding to libleidenalg's MarketNullModularityVertexPartition "
            "(market-aggregated configuration-null modularity).";

  m.def("optimise_market_null", &optimise_market_null,
        py::arg("n_vertices"),
        py::arg("edge_src"), py::arg("edge_dst"), py::arg("edge_weight"),
        py::arg("strength_node"), py::arg("strength_layer"),
        py::arg("strength_out"), py::arg("strength_in"),
        py::arg("layer_total_weight"),
        py::arg("resolution") = 1.0, py::arg("seed") = 0, py::arg("directed") = false,
        py::arg("fixed_nodes") = IntArray(0), py::arg("reassign_fixed_at_end") = false,
        "Optimise a single shared community assignment under the per-layer "
        "configuration-null modularity. Returns (membership, quality).\n\n"
        "strength_* are parallel sparse arrays of per-(node, layer) strengths; "
        "strength_in is ignored when directed=False. layer_total_weight is "
        "indexed by dense layer id (0..L-1).\n\n"
        "fixed_nodes: node ids frozen in their initial singleton community during "
        "optimisation (e.g. high-participation hub/market-maker wallets), which "
        "removes their dominant per-move cost. reassign_fixed_at_end: if True, run "
        "one final local-move sweep that reassigns only those frozen nodes.");

  m.def("market_null_quality", &market_null_quality,
        py::arg("n_vertices"),
        py::arg("edge_src"), py::arg("edge_dst"), py::arg("edge_weight"),
        py::arg("strength_node"), py::arg("strength_layer"),
        py::arg("strength_out"), py::arg("strength_in"),
        py::arg("layer_total_weight"),
        py::arg("membership"),
        py::arg("resolution") = 1.0, py::arg("directed") = false,
        "Score a fixed membership under the per-layer configuration-null "
        "modularity. Returns the quality (float).");
}
