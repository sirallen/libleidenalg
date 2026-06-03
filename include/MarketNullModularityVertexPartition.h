#ifndef MARKETNULLMODULARITYVERTEXPARTITION_H
#define MARKETNULLMODULARITYVERTEXPARTITION_H

#include "LinearResolutionParameterVertexPartition.h"
#include <map>

/****************************************************************************
Modularity with a per-layer ("market-aggregated") configuration null.

This is multislice/RB-configuration modularity for the special case of a single
shared (singular) community membership across all layers. Equivalently, it is
the standard generalised modularity

    Q = sum_ij [ A_ij - gamma * P_ij ] delta(c_i, c_j)

on a single node set, where the observed term A is the aggregate graph supplied
as the igraph `Graph`, and the null is a configuration model computed *per
layer* (e.g. per market) and summed: a random edge may only connect two nodes
via a layer in which both participate.

For undirected graphs the null is

    P_ij = sum_s k_i^(s) k_j^(s) / (2 m_s),

and for directed graphs it is the Leicht-Newman directed form

    P_ij = sum_s k_i^out,(s) k_j^in,(s) / m_s.

With a single layer spanning the whole graph this reduces exactly to
RBConfigurationVertexPartition (undirected or directed respectively).

The per-layer node strengths and per-layer totals m_s are carried by the `Graph`
(see Graph::set_layer_strengths{,_directed}); they aggregate correctly under
graph collapse. This class maintains the per-community, per-layer out/in
strength sums K_c^out,(s), K_c^in,(s) incrementally, so its memory scales with
total layer participation (~ data size), not with (#layers x #nodes).
*****************************************************************************/

class LIBLEIDENALG_EXPORT MarketNullModularityVertexPartition : public LinearResolutionParameterVertexPartition
{
  public:
    MarketNullModularityVertexPartition(Graph* graph,
          vector<size_t> const& membership, double resolution_parameter);
    MarketNullModularityVertexPartition(Graph* graph,
          vector<size_t> const& membership);
    MarketNullModularityVertexPartition(Graph* graph,
          double resolution_parameter);
    MarketNullModularityVertexPartition(Graph* graph);
    virtual ~MarketNullModularityVertexPartition();

    virtual MarketNullModularityVertexPartition* create(Graph* graph);
    virtual MarketNullModularityVertexPartition* create(Graph* graph, vector<size_t> const& membership);

    virtual double diff_move(size_t v, size_t new_comm);
    virtual double quality(double resolution_parameter);

  protected:
    virtual void init_admin_extra();
    virtual void relocate_node(size_t v, size_t old_comm, size_t new_comm);
    virtual void relabel_communities_extra(vector<size_t> const& new_comm_id);

  private:
    // Per-community, per-layer out/in strength sums K_c^out,(s), K_c^in,(s)
    // (sparse over layers). For undirected graphs the two maps are identical.
    vector< map<size_t, double> > _out_strength_in_comm;
    vector< map<size_t, double> > _in_strength_in_comm;

    void build_layer_strength_admin();

    static inline double lookup(vector< map<size_t, double> > const& K, size_t comm, size_t layer)
    {
      if (comm >= K.size())
        return 0.0;
      map<size_t, double>::const_iterator it = K[comm].find(layer);
      if (it == K[comm].end())
        return 0.0;
      return it->second;
    }

    // K_c^out,(s) / K_c^in,(s), returning 0 for unknown community/layer.
    inline double out_strength_in_comm(size_t comm, size_t layer)
    { return lookup(this->_out_strength_in_comm, comm, layer); }
    inline double in_strength_in_comm(size_t comm, size_t layer)
    { return lookup(this->_in_strength_in_comm, comm, layer); }
};

#endif // MARKETNULLMODULARITYVERTEXPARTITION_H
