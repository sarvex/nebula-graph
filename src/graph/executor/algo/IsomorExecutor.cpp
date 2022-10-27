// Copyright (c) 2020 vesoft inc. All rights reserved.
//
// This source code is licensed under Apache 2.0 License.
#include "graph/executor/algo/IsomorExecutor.h"

#include <fstream>
#include <unordered_map>
#include <vector>

#include "graph/executor/subgraph_provenance/graph.h"
#include "graph/executor/subgraph_provenance/subgraph.h"
#include "graph/planner/plan/Algo.h"
namespace nebula {
namespace graph {

static const char kDefaultProp[] = "default";  //

folly::Future<Status> IsomorExecutor::execute() {
  // TODO: Replace the following codes with subgraph matching. Return type.
  // Define 2:
  SCOPED_TIMER(&execTime_);
  auto* isomor = asNode<Isomor>(node());
  DataSet ds;
  ds.colNames = isomor->colNames();
  auto iterDV = ectx_->getResult(isomor->getdScanVOut()).iter();
  auto iterQV = ectx_->getResult(isomor->getqScanVOut()).iter();
  auto iterDE = ectx_->getResult(isomor->getdScanEOut()).iter();
  auto iterQE = ectx_->getResult(isomor->getqScanEOut()).iter();
  unsigned int v_count = iterDV->size();
  unsigned int l_count = iterDV->size();
  unsigned int e_count = iterDE->size();
  // Example:
  // Vetices 3: 0, 1, 2, 3
  // Edges:
  // 0 1
  // 1 2
  // 2 3
  // 3 0
  // To store the degree of each vertex
  unsigned int* degree = new unsigned int[v_count];
  // degree[0] = 2
  // degree[1] = 2
  // degree[2] = 2
  // degree[3] = 2
  // To store the starting position of each vertex in neighborhood array.

  unsigned int* offset = new unsigned int[v_count + 1];
  // offset[0] = 0
  // offset[1] = 2
  // offset[2] = 4
  // offset[3] = 6
  // offset[4] = 8 // End of the neighborhood array

  // Array of the neighborhood can be initialized by 2 dimension of the matrix,
  // However, here we use 2*edge count as we have in edge and out edges.
  unsigned int* neighbors = new unsigned int[e_count * 2];
  // neighbors[0] = 1
  // neighbors[1] = 3
  // neighbors[2] = 0
  // neighbors[3] = 2
  // neighbors[4] = 1
  // neighbors[5] = 3
  // neighbors[6] = 2
  // neighbors[7] = 0

  unsigned int* labels = new unsigned int[l_count];

  // Initialize the degree for data graph
  for (unsigned int i = 0; i < v_count; i++) {
    degree[i] = 0;
  }

  // load data vertices id and tags
  while (iterDV->valid()) {
    const auto vertex = iterDV->getColumn(nebula::kVid);  // check if v is a vertex
    auto v_id = vertex.getInt();
    const auto label = iterDV->getColumn(nebula::graph::kDefaultProp);  // get label by index
    auto l_id = label.getInt();
    // unsigned int v_id = (unsigned int)v.getInt(0);
    labels[v_id] = l_id;  // Tag Id
    iterDV->next();
  }

  // load edges degree
  while (iterDE->valid()) {
    auto s = iterDE->getEdgeProp("*", kSrc);
    unsigned int src = s.getInt();
    degree[src]++;
    iterDE->next();
  }

  // caldulate the start position of each vertex in the neighborhood array
  for (unsigned int i = 0; i < v_count; i++) {
    offset[i + 1] += degree[i] + offset[i];
  }

  // load data edges
  offset[0] = 0;
  iterDE = ectx_->getResult(isomor->getdScanEOut()).iter();
  while (iterDE->valid()) {
    unsigned int src = iterDE->getEdgeProp("*", kSrc).getInt();
    unsigned int dst = iterDE->getEdgeProp("*", kDst).getInt();

    neighbors[offset[src + 1]] = dst;
    offset[src + 1]++;
    iterDE->next();
  }
  for (unsigned int i = 0; i < v_count; i++) {
    offset[i + 1] = offset[i];
  }

  Graph* data_graph = new Graph();
  data_graph->loadGraphFromExecutor(v_count, l_count, e_count, offset, neighbors, labels);

  // load query vertices id and tags
  while (iterQV->valid()) {
    const auto vertex = iterQV->getColumn(nebula::kVid);  // check if v is a vertex
    auto v_id = vertex.getInt();
    const auto label = iterQV->getColumn(nebula::graph::kDefaultProp);  // get label by index
    auto l_id = label.getInt();
    // unsigned int v_id = (unsigned int)v.getInt(0);
    labels[v_id] = l_id;  // Tag Id
    iterQV->next();
  }

  // Initialize the degree for query graph
  for (unsigned int i = 0; i < v_count; i++) {
    degree[i] = 0;
  }

  // load query edges degree
  while (iterQE->valid()) {
    auto s = iterQE->getEdgeProp("*", kSrc);
    unsigned int src = s.getInt();
    offset[src]++;
    iterDE->next();
  }

  // caldulate the start position of each vertex in the neighborhood array
  for (unsigned int i = 0; i < v_count; i++) {
    offset[i + 1] += offset[i];
  }

  // load query edges
  offset[0] = 0;
  iterQE = ectx_->getResult(isomor->getdScanEOut()).iter();
  while (iterDE->valid()) {
    unsigned int src = iterQE->getEdgeProp("*", kSrc).getInt();
    unsigned int dst = iterQE->getEdgeProp("*", kDst).getInt();

    neighbors[offset[src + 1]] = dst;
    offset[src + 1]++;
    iterQE->next();
  }
  for (unsigned int i = 0; i < v_count; i++) {
    offset[i + 1] = offset[i];
  }

  Graph* query_graph = new Graph();
  query_graph->loadGraphFromExecutor(v_count, l_count, e_count, offset, neighbors, labels);

  ui** candidates = nullptr;
  ui* candidates_count = nullptr;

  TreeNode* ceci_tree = nullptr;
  ui* ceci_order = nullptr;
  ui* provenance = nullptr;

  std::vector<std::unordered_map<V_ID, std::vector<V_ID>>>
      P_Candidates;  //  Parent, first branch, second branch.
  std::vector<std::unordered_map<V_ID, std::vector<V_ID>>> P_Provenance;
  // std::cout"Provenance Function: " << std::endl:endl;

  bool result = CECIFunction(data_graph,
                             query_graph,
                             candidates,
                             candidates_count,
                             ceci_order,
                             provenance,
                             ceci_tree,
                             P_Candidates,
                             P_Provenance);
  delete data_graph;
  delete query_graph;
  delete[] ceci_order;
  delete[] provenance;
  delete[] candidates_count;
  delete[] candidates;
  delete ceci_tree;

  delete[] offset;
  delete[] neighbors;
  delete[] labels;
  ResultBuilder builder;

  // Set result in the ds and set the new column name for the (isomor matching 's) result.
  return finish(ResultBuilder().value(Value(std::move(result))).build());
}
}  // namespace graph
}  // namespace nebula