#include "MarketNullModularityVertexPartition.h"

#include <cmath>
#include <utility>

namespace {
// Move strength k from old_comm to new_comm within layer s of a
// per-community/per-layer sum map, dropping residual entries.
inline void relocate_layer(vector< map<size_t, double> >& K,
    size_t old_comm, size_t new_comm, size_t s, double k)
{
  if (k != 0.0)
  {
    map<size_t, double>& old_map = K[old_comm];
    map<size_t, double>::iterator it = old_map.find(s);
    if (it != old_map.end())
    {
      it->second -= k;
      if (std::fabs(it->second) < 1e-12)
        old_map.erase(it);
    }
    K[new_comm][s] += k;
  }
}
} // namespace

MarketNullModularityVertexPartition::MarketNullModularityVertexPartition(Graph* graph,
      vector<size_t> const& membership, double resolution_parameter) :
        LinearResolutionParameterVertexPartition(graph, membership, resolution_parameter)
{
  this->build_layer_strength_admin();
}

MarketNullModularityVertexPartition::MarketNullModularityVertexPartition(Graph* graph,
      vector<size_t> const& membership) :
        LinearResolutionParameterVertexPartition(graph, membership)
{
  this->build_layer_strength_admin();
}

MarketNullModularityVertexPartition::MarketNullModularityVertexPartition(Graph* graph,
      double resolution_parameter) :
        LinearResolutionParameterVertexPartition(graph, resolution_parameter)
{
  this->build_layer_strength_admin();
}

MarketNullModularityVertexPartition::MarketNullModularityVertexPartition(Graph* graph) :
        LinearResolutionParameterVertexPartition(graph)
{
  this->build_layer_strength_admin();
}

MarketNullModularityVertexPartition::~MarketNullModularityVertexPartition()
{ }

MarketNullModularityVertexPartition* MarketNullModularityVertexPartition::create(Graph* graph)
{
  return new MarketNullModularityVertexPartition(graph, this->resolution_parameter);
}

MarketNullModularityVertexPartition* MarketNullModularityVertexPartition::create(Graph* graph, vector<size_t> const& membership)
{
  return new MarketNullModularityVertexPartition(graph, membership, this->resolution_parameter);
}

/*****************************************************************************
  Build the per-community, per-layer strength sums K_c^(s) from scratch based on
  the current membership and the graph's per-layer node strengths. Invoked at
  construction and via init_admin_extra() on every full admin rebuild.
*****************************************************************************/
void MarketNullModularityVertexPartition::build_layer_strength_admin()
{
  if (!this->graph->has_layer_strengths())
    throw Exception("MarketNullModularityVertexPartition requires per-layer node strengths (see Graph::set_layer_strengths{,_directed}).");

  size_t n = this->graph->vcount();

  this->_out_strength_in_comm.clear();
  this->_out_strength_in_comm.resize(this->n_communities());
  this->_in_strength_in_comm.clear();
  this->_in_strength_in_comm.resize(this->n_communities());

  for (size_t v = 0; v < n; v++)
  {
    size_t v_comm = this->membership(v);
    for (LayerStrength const& ls : this->graph->node_layer_strength(v))
    {
      this->_out_strength_in_comm[v_comm][ls.layer] += ls.out_strength;
      this->_in_strength_in_comm[v_comm][ls.layer] += ls.in_strength;
    }
  }
}

void MarketNullModularityVertexPartition::init_admin_extra()
{
  this->build_layer_strength_admin();
}

/*****************************************************************************
  Incrementally update the per-community, per-layer strength sums when node v
  moves from old_comm to new_comm. Only v's own (sparse) per-layer strengths are
  touched, so this is O(#layers in which v participates).
*****************************************************************************/
void MarketNullModularityVertexPartition::relocate_node(size_t v, size_t old_comm, size_t new_comm)
{
  if (old_comm == new_comm)
    return;

  // move_node() may have just created new_comm as an empty community; make sure
  // our community-indexed admin is large enough.
  if (this->_out_strength_in_comm.size() < this->n_communities())
    this->_out_strength_in_comm.resize(this->n_communities());
  if (this->_in_strength_in_comm.size() < this->n_communities())
    this->_in_strength_in_comm.resize(this->n_communities());

  for (LayerStrength const& ls : this->graph->node_layer_strength(v))
  {
    relocate_layer(this->_out_strength_in_comm, old_comm, new_comm, ls.layer, ls.out_strength);
    relocate_layer(this->_in_strength_in_comm, old_comm, new_comm, ls.layer, ls.in_strength);
  }
}

/*****************************************************************************
  Permute the community-indexed admin to match a community relabeling. Runs
  while the base bookkeeping (cnodes) still reflects the pre-relabel communities.
*****************************************************************************/
void MarketNullModularityVertexPartition::relabel_communities_extra(vector<size_t> const& new_comm_id)
{
  size_t nbcomms = this->n_communities();
  vector< map<size_t, double> > new_out(nbcomms);
  vector< map<size_t, double> > new_in(nbcomms);

  for (size_t c = 0; c < new_comm_id.size(); c++)
  {
    if (this->cnodes(c) > 0)
    {
      size_t new_c = new_comm_id[c];
      if (new_c < nbcomms)
      {
        if (c < this->_out_strength_in_comm.size())
          new_out[new_c] = std::move(this->_out_strength_in_comm[c]);
        if (c < this->_in_strength_in_comm.size())
          new_in[new_c] = std::move(this->_in_strength_in_comm[c]);
      }
    }
  }

  this->_out_strength_in_comm = std::move(new_out);
  this->_in_strength_in_comm = std::move(new_in);
}

/*****************************************************************************
  Difference in quality from moving node v to new_comm. The observed (aggregate
  adjacency) term is identical to RBConfigurationVertexPartition; only the null
  term is replaced by the per-layer configuration null.
*****************************************************************************/
double MarketNullModularityVertexPartition::diff_move(size_t v, size_t new_comm)
{
  size_t old_comm = this->_membership[v];
  if (new_comm == old_comm)
    return 0.0;

  double is_directed = this->graph->is_directed();

  // Observed term (aggregate graph), mirroring RBConfigurationVertexPartition.
  double w_to_old = this->weight_to_comm(v, old_comm);
  double w_from_old = this->weight_from_comm(v, old_comm);
  double w_to_new = this->weight_to_comm(v, new_comm);
  double w_from_new = this->weight_from_comm(v, new_comm);
  double self_weight = this->graph->node_self_weight(v);

  double diff_obs = (w_to_new + self_weight + w_from_new + self_weight)
                  - (w_to_old + w_from_old);

  // Per-layer configuration null. For each layer s in which v participates,
  // moving v changes sum_c K_c^out,(s) K_c^in,(s) by
  //   a_in (K_new0^out - K_old^out) + a_out (K_new0^in - K_old^in) + 2 a_out a_in,
  // where K_old includes v (out/in) and K_new0 excludes v, and a_out/a_in are
  // v's out/in strength in layer s. For undirected graphs a_out == a_in and the
  // out/in community sums coincide, recovering the symmetric configuration null.
  double null_norm = (is_directed ? 1.0 : 4.0);
  double null_acc = 0.0;
  for (LayerStrength const& ls : this->graph->node_layer_strength(v))
  {
    size_t s = ls.layer;
    double m_s = this->graph->layer_total_weight(s);
    if (m_s <= 0.0)
      continue;

    double a_out = ls.out_strength;
    double a_in = ls.in_strength;

    double K_old_out = this->out_strength_in_comm(old_comm, s);  // includes v
    double K_old_in = this->in_strength_in_comm(old_comm, s);
    double K_new0_out = this->out_strength_in_comm(new_comm, s); // excludes v
    double K_new0_in = this->in_strength_in_comm(new_comm, s);

    double delta = a_in * (K_new0_out - K_old_out)
                 + a_out * (K_new0_in - K_old_in)
                 + 2.0 * a_out * a_in;
    null_acc += delta / (null_norm * m_s);
  }

  double diff_null = -(2.0 - is_directed) * this->resolution_parameter * null_acc;

  return diff_obs + diff_null;
}

/*****************************************************************************
  Quality: observed aggregate modularity minus the per-layer configuration null,
  using the unnormalised (RBConfiguration-style) scaling. With a single layer
  spanning the whole graph this equals RBConfigurationVertexPartition::quality.
*****************************************************************************/
double MarketNullModularityVertexPartition::quality(double resolution_parameter)
{
  double is_directed = this->graph->is_directed();

  double q_obs = 0.0;
  for (size_t c = 0; c < this->n_communities(); c++)
    q_obs += this->total_weight_in_comm(c);

  double null_norm = (is_directed ? 1.0 : 4.0);
  double q_null = 0.0;
  size_t n_comms = this->n_communities();
  // A layer contributes K_c^out K_c^in; iterating the out-map and looking up the
  // in-map captures every nonzero product (a nonzero product requires both).
  for (size_t c = 0; c < n_comms && c < this->_out_strength_in_comm.size(); c++)
  {
    for (std::pair<const size_t, double> const& entry : this->_out_strength_in_comm[c])
    {
      double m_s = this->graph->layer_total_weight(entry.first);
      if (m_s <= 0.0)
        continue;
      double K_out = entry.second;
      double K_in = this->in_strength_in_comm(c, entry.first);
      q_null += K_out * K_in / (null_norm * m_s);
    }
  }

  return (2.0 - is_directed) * (q_obs - resolution_parameter * q_null);
}
