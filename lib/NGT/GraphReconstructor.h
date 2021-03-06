//
// Copyright (C) 2015-2020 Yahoo Japan Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include	<unordered_map>
#include	<unordered_set>
#include	<list>

#ifdef _OPENMP
#include	<omp.h>
#else
#warning "*** OMP is *NOT* available! ***"
#endif

namespace NGT {

class GraphReconstructor {
 public:
  static void extractGraph(std::vector<NGT::ObjectDistances> &graph, NGT::Index &index) {
    NGT::GraphIndex	&graphIndex = static_cast<NGT::GraphIndex&>(index.getIndex());
    graph.reserve(graphIndex.repository.size());
    for (size_t id = 1; id < graphIndex.repository.size(); id++) {
      if (id % 1000000 == 0) {
	std::cerr << "GraphReconstructor::extractGraph: Processed " << id << " objects." << std::endl;
      }
      try {
	NGT::GraphNode &node = *graphIndex.getNode(id);
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	NGT::ObjectDistances nd;
	nd.reserve(node.size());
	for (auto n = node.begin(graphIndex.repository.allocator); n != node.end(graphIndex.repository.allocator); ++n) {
	  nd.push_back(ObjectDistance((*n).id, (*n).distance));
        }
	graph.push_back(nd);
#else
	graph.push_back(node);
#endif
	if (graph.back().size() != graph.back().capacity()) {
	  std::cerr << "GraphReconstructor::extractGraph: Warning! The graph size must be the same as the capacity. " << id << std::endl;
	}
      } catch(NGT::Exception &err) {
	std::cerr << "GraphReconstructor::extractGraph: Warning! Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
	continue;
      }
    }

  }




  static void 
    adjustPaths(NGT::Index &outIndex)
  {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
    std::cerr << "construct index is not implemented." << std::endl;
    exit(1);
#else
    NGT::GraphIndex	&outGraph = dynamic_cast<NGT::GraphIndex&>(outIndex.getIndex());
    size_t rStartRank = 0; 
    std::list<std::pair<size_t, NGT::GraphNode> > tmpGraph;
    for (size_t id = 1; id < outGraph.repository.size(); id++) {
      NGT::GraphNode &node = *outGraph.getNode(id);
      tmpGraph.push_back(std::pair<size_t, NGT::GraphNode>(id, node));
      if (node.size() > rStartRank) {
	node.resize(rStartRank);
      }
    }
    size_t removeCount = 0;
    for (size_t rank = rStartRank; ; rank++) {
      bool edge = false;
      Timer timer;
      for (auto it = tmpGraph.begin(); it != tmpGraph.end();) {
	size_t id = (*it).first;
	try {
	  NGT::GraphNode &node = (*it).second;
	  if (rank >= node.size()) {
	    it = tmpGraph.erase(it);
	    continue;
	  }
	  edge = true;
	  if (rank >= 1 && node[rank - 1].distance > node[rank].distance) {
	    std::cerr << "distance order is wrong!" << std::endl;
	    std::cerr << id << ":" << rank << ":" << node[rank - 1].id << ":" << node[rank].id << std::endl;	    
	  }
	  NGT::GraphNode &tn = *outGraph.getNode(id);
	  //////////////////
	  volatile bool found = false;
	  if (rank < 1000) {
	    for (size_t tni = 0; tni < tn.size() && !found; tni++) {
	      if (tn[tni].id == node[rank].id) {
		continue;
	      }
	      NGT::GraphNode &dstNode = *outGraph.getNode(tn[tni].id);
	      for (size_t dni = 0; dni < dstNode.size(); dni++) {
		if ((dstNode[dni].id == node[rank].id) && (dstNode[dni].distance < node[rank].distance)) {
		  found = true;
		  break;
		}
	      } 
	    }  
	  } else {
#ifdef _OPENMP
#pragma omp parallel for num_threads(10)
#endif
	    for (size_t tni = 0; tni < tn.size(); tni++) {
	      if (found) {
		continue;
	      }
	      if (tn[tni].id == node[rank].id) {
		continue;
	      }
	      NGT::GraphNode &dstNode = *outGraph.getNode(tn[tni].id);
	      for (size_t dni = 0; dni < dstNode.size(); dni++) {
		if ((dstNode[dni].id == node[rank].id) && (dstNode[dni].distance < node[rank].distance)) {
		  found = true;
		}
	      } 
	    } 
	  } 
	  if (!found) {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	    outGraph.addEdge(id, node.at(i, outGraph.repository.allocator).id,
			     node.at(i, outGraph.repository.allocator).distance, true);
#else
	    tn.push_back(NGT::ObjectDistance(node[rank].id, node[rank].distance));
#endif
	  } else {
	    removeCount++;
	  }
	} catch(NGT::Exception &err) {
	  std::cerr << "GraphReconstructor: Warning. Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
	  it++;
	  continue;
	}
	it++;
      } 
      if (edge == false) {
	break;
      }
    } 
#endif // NGT_SHARED_MEMORY_ALLOCATOR
  }

  static void 
    adjustPathsEffectively(NGT::Index &outIndex)
  {
    NGT::GraphIndex	&outGraph = dynamic_cast<NGT::GraphIndex&>(outIndex.getIndex());
    adjustPathsEffectively(outGraph);
  }

  static void 
    adjustPathsEffectively(NGT::GraphIndex &outGraph)
  {
    Timer timer;
    timer.start();
    size_t rStartRank = 0; 
    std::vector<std::pair<size_t, NGT::GraphNode> > tmpGraph;
    for (size_t id = 1; id < outGraph.repository.size(); id++) {
      NGT::GraphNode &node = *outGraph.getNode(id);
      tmpGraph.push_back(std::pair<size_t, NGT::GraphNode>(id, node));
      if (node.size() > rStartRank) {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	node.resize(rStartRank, outGraph.repository.allocator);
#else
	node.resize(rStartRank);
#endif
      }
    }
    timer.stop();
    std::cerr << "GraphReconstructor::adjustPaths: graph preparing time=" << timer << std::endl;
    timer.reset();
    timer.start();

    std::vector<std::vector<std::pair<uint32_t, uint32_t> > > removeCandidates(tmpGraph.size());
    int removeCandidateCount = 0;
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (size_t idx = 0; idx < tmpGraph.size(); ++idx) {
      auto it = tmpGraph.begin() + idx;
      size_t id = (*it).first;
      try {
	NGT::GraphNode &srcNode = (*it).second;	
	std::unordered_map<uint32_t, std::pair<size_t, double> > neighbors;
	for (size_t sni = 0; sni < srcNode.size(); ++sni) {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	  neighbors[srcNode.at(sni, outGraph.repository.allocator).id] = std::pair<size_t, double>(sni, srcNode.at(sni, outGraph.repository.allocator).distance);
#else
	  neighbors[srcNode[sni].id] = std::pair<size_t, double>(sni, srcNode[sni].distance);
#endif
	}

	std::vector<std::pair<int, std::pair<uint32_t, uint32_t> > > candidates;	
	for (size_t sni = 0; sni < srcNode.size(); sni++) { 
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	  assert(srcNode.at(sni, outGraph.repository.allocator).id == tmpGraph[srcNode.at(sni, outGraph.repository.allocator).id - 1].first);
	  NGT::GraphNode &pathNode = tmpGraph[srcNode.at(sni, outGraph.repository.allocator).id - 1].second;
#else
	  assert(srcNode[sni].id == tmpGraph[srcNode[sni].id - 1].first);
	  NGT::GraphNode &pathNode = tmpGraph[srcNode[sni].id - 1].second;
#endif
	  for (size_t pni = 0; pni < pathNode.size(); pni++) {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	    auto dstNodeID = pathNode.at(pni, outGraph.repository.allocator).id;
#else
	    auto dstNodeID = pathNode[pni].id;
#endif
	    auto dstNode = neighbors.find(dstNodeID);
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	    if (dstNode != neighbors.end() 
		&& srcNode.at(sni, outGraph.repository.allocator).distance < (*dstNode).second.second 
		&& pathNode.at(pni, outGraph.repository.allocator).distance < (*dstNode).second.second  
		) {
#else
	    if (dstNode != neighbors.end() 
		&& srcNode[sni].distance < (*dstNode).second.second 
		&& pathNode[pni].distance < (*dstNode).second.second  
		) {
#endif
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	      candidates.push_back(std::pair<int, std::pair<uint32_t, uint32_t> >((*dstNode).second.first, std::pair<uint32_t, uint32_t>(srcNode.at(sni, outGraph.repository.allocator).id, dstNodeID)));  
#else
	      candidates.push_back(std::pair<int, std::pair<uint32_t, uint32_t> >((*dstNode).second.first, std::pair<uint32_t, uint32_t>(srcNode[sni].id, dstNodeID)));  
#endif
	      removeCandidateCount++;
	    }
	  }
	}
	sort(candidates.begin(), candidates.end(), std::greater<std::pair<int, std::pair<uint32_t, uint32_t>>>());
	for (size_t i = 0; i < candidates.size(); i++) {
	  removeCandidates[idx].push_back(candidates[i].second);
	}
      } catch(NGT::Exception &err) {
	std::cerr << "GraphReconstructor: Warning. Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
	continue;
      }
    }
    timer.stop();
    std::cerr << "GraphReconstructor::adjustPaths extracting removed edge candidates time=" << timer << std::endl;
    timer.reset();
    timer.start();

    std::list<size_t> ids;
    for (auto it = tmpGraph.begin(); it != tmpGraph.end(); ++it) {
      size_t id = (*it).first;
      ids.push_back(id);
    }

    int removeCount = 0;
    removeCandidateCount = 0;
    std::vector<std::unordered_set<uint32_t> > edges(tmpGraph.size()); 
    for (size_t rank = 0; ids.size() != 0; rank++) {
      for (auto it = ids.begin(); it != ids.end(); ) {
	size_t id = *it;
	size_t idx = id - 1;
	try {
	  NGT::GraphNode &srcNode = tmpGraph[idx].second;
	  if (rank >= srcNode.size()) {
	    if (!removeCandidates[idx].empty()) {
	      std::cerr << "Something wrong! ID=" << id << " # of remaining candidates=" << removeCandidates[idx].size() << std::endl;
	      abort();
	    }
	    it = ids.erase(it);
	    continue;
	  }
	  if (removeCandidates[idx].size() > 0) {
	    removeCandidateCount++;
	    bool pathExist = false;
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
            while (!removeCandidates[idx].empty() && (removeCandidates[idx].back().second == srcNode.at(rank, outGraph.repository.allocator).id)) {
#else
	    while (!removeCandidates[idx].empty() && (removeCandidates[idx].back().second == srcNode[rank].id)) {
#endif
	      size_t path = removeCandidates[idx].back().first;
	      size_t dst = removeCandidates[idx].back().second;
	      removeCandidates[idx].pop_back();
	      if ((edges[idx].find(path) != edges[idx].end()) && (edges[path - 1].find(dst) != edges[path - 1].end())) {
		pathExist = true;
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	        while (!removeCandidates[idx].empty() && (removeCandidates[idx].back().second == srcNode.at(rank, outGraph.repository.allocator).id)) {
#else
		while (!removeCandidates[idx].empty() && (removeCandidates[idx].back().second == srcNode[rank].id)) {
#endif
	          removeCandidates[idx].pop_back();
		}
		break;
	      }
	    }
	    if (pathExist) {
	      removeCount++;
              it++;
	      continue;
	    }
	  }
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	  edges[idx].insert(srcNode.at(rank, outGraph.repository.allocator).id);
#else
	  edges[idx].insert(srcNode[rank].id);
#endif
	  NGT::GraphNode &outSrcNode = *outGraph.getNode(id);
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	  outSrcNode.push_back(NGT::ObjectDistance(srcNode.at(rank, outGraph.repository.allocator).id, srcNode.at(rank, outGraph.repository.allocator).distance), outGraph.repository.allocator);
#else
          size_t r = outSrcNode.capacity();
          size_t s = outSrcNode.size();
	  outSrcNode.push_back(NGT::ObjectDistance(srcNode[rank].id, srcNode[rank].distance));
          if (r != outSrcNode.capacity()) {
             std::cerr << id << "-" << rank << " " << s << ":" << r << ":" << outSrcNode.capacity() << std::endl;
          }
#endif
	} catch(NGT::Exception &err) {
	  std::cerr << "GraphReconstructor: Warning. Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
          it++;
	  continue;
	}
        it++;
      }
    }
  }

  static 
    void convertToANNG(std::vector<NGT::ObjectDistances> &graph)
  {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
    std::cerr << "convertToANNG is not implemented for shared memory." << std::endl;
    return;
#else
    std::cerr << "convertToANNG begin" << std::endl;
    for (size_t idx = 0; idx < graph.size(); idx++) {
      NGT::GraphNode &node = graph[idx];
      for (auto ni = node.begin(); ni != node.end(); ++ni) {
	graph[(*ni).id - 1].push_back(NGT::ObjectDistance(idx + 1, (*ni).distance));
      }
    }
    for (size_t idx = 0; idx < graph.size(); idx++) {
      NGT::GraphNode &node = graph[idx];
      if (node.size() == 0) {
	continue;
      }
      std::sort(node.begin(), node.end());
      NGT::ObjectID prev = 0;
      for (auto it = node.begin(); it != node.end();) {
	if (prev == (*it).id) {
	  it = node.erase(it);
	  continue;
	}
	prev = (*it).id;
	  it++;
      }
      NGT::GraphNode tmp = node;
      node.swap(tmp);
    }
    std::cerr << "convertToANNG end" << std::endl;
#endif
  }

  static 
    void reconstructGraph(std::vector<NGT::ObjectDistances> &graph, NGT::Index &outIndex, size_t originalEdgeSize, size_t reverseEdgeSize) 
  {
    if (reverseEdgeSize > 10000) {
      std::cerr << "something wrong. Edge size=" << reverseEdgeSize << std::endl;
      exit(1);
    }

    NGT::Timer	originalEdgeTimer, reverseEdgeTimer, normalizeEdgeTimer;
    originalEdgeTimer.start();
    NGT::GraphIndex	&outGraph = dynamic_cast<NGT::GraphIndex&>(outIndex.getIndex());

    for (size_t id = 1; id < outGraph.repository.size(); id++) {
      try {
	NGT::GraphNode &node = *outGraph.getNode(id);
	if (originalEdgeSize == 0) {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	  node.clear(outGraph.repository.allocator);
#else
	  NGT::GraphNode empty;
	  node.swap(empty);
#endif
	} else {
	  NGT::ObjectDistances n = graph[id - 1];
	  if (n.size() < originalEdgeSize) {
	    std::cerr << "node size is too few." << std::endl;
	    std::cerr << n.size() << ":" << originalEdgeSize << std::endl;
	    continue;
	  }
	  n.resize(originalEdgeSize);
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	  node.copy(n, outGraph.repository.allocator);
#else
	  node.swap(n);
#endif
	}
      } catch(NGT::Exception &err) {
	std::cerr << "GraphReconstructor: Warning. Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
	continue;
      }
    }
    originalEdgeTimer.stop();

    reverseEdgeTimer.start();
    int insufficientNodeCount = 0;
    for (size_t id = 1; id <= graph.size(); ++id) {
      try {
	NGT::ObjectDistances &node = graph[id - 1];
	size_t rsize = reverseEdgeSize;
	if (rsize > node.size()) {
	  insufficientNodeCount++;
	  rsize = node.size();
	}
	for (size_t i = 0; i < rsize; ++i) {
	  NGT::Distance distance = node[i].distance;
	  size_t nodeID = node[i].id;
	  try {
	    NGT::GraphNode &n = *outGraph.getNode(nodeID);
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	    n.push_back(NGT::ObjectDistance(id, distance), outGraph.repository.allocator);
#else
	    n.push_back(NGT::ObjectDistance(id, distance));
#endif
	  } catch(...) {}
	}
      } catch(NGT::Exception &err) {
	std::cerr << "GraphReconstructor: Warning. Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
	continue;
      }
    } 
    reverseEdgeTimer.stop();    
    if (insufficientNodeCount != 0) {
      std::cerr << "# of the nodes edges of which are in short = " << insufficientNodeCount << std::endl;
    }

    normalizeEdgeTimer.start();    
    for (size_t id = 1; id < outGraph.repository.size(); id++) {
      try {
	NGT::GraphNode &n = *outGraph.getNode(id);
	if (id % 100000 == 0) {
	  std::cerr << "Processed " << id << " nodes" << std::endl;
	}
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	std::sort(n.begin(outGraph.repository.allocator), n.end(outGraph.repository.allocator));
#else
	std::sort(n.begin(), n.end());
#endif
	NGT::ObjectID prev = 0;
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	for (auto it = n.begin(outGraph.repository.allocator); it != n.end(outGraph.repository.allocator);) {
#else
	for (auto it = n.begin(); it != n.end();) {
#endif
	  if (prev == (*it).id) {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
	    it = n.erase(it, outGraph.repository.allocator);
#else
	    it = n.erase(it);
#endif
	    continue;
	  }
	  prev = (*it).id;
	  it++;
	}
#if !defined(NGT_SHARED_MEMORY_ALLOCATOR)
	NGT::GraphNode tmp = n;
	n.swap(tmp);
#endif
      } catch (...) {
	std::cerr << "Graph::construct: error. something wrong. ID=" << id << std::endl;
      }
    }
    normalizeEdgeTimer.stop();
    std::cerr << "Reconstruction time=" << originalEdgeTimer.time << ":" << reverseEdgeTimer.time 
	 << ":" << normalizeEdgeTimer.time << std::endl;
    std::cerr << "original edge size=" << originalEdgeSize << std::endl;
    std::cerr << "reverse edge size=" << reverseEdgeSize << std::endl;
  }



  static 
    void reconstructGraphWithConstraint(std::vector<NGT::ObjectDistances> &graph, NGT::Index &outIndex, 
					size_t originalEdgeSize, size_t reverseEdgeSize,
					char mode = 'a') 
  {
#if defined(NGT_SHARED_MEMORY_ALLOCATOR)
    std::cerr << "reconstructGraphWithConstraint is not implemented." << std::endl;
    abort();
#else 

    NGT::Timer	originalEdgeTimer, reverseEdgeTimer, normalizeEdgeTimer;

    if (reverseEdgeSize > 10000) {
      std::cerr << "something wrong. Edge size=" << reverseEdgeSize << std::endl;
      exit(1);
    }
    NGT::GraphIndex	&outGraph = dynamic_cast<NGT::GraphIndex&>(outIndex.getIndex());

    for (size_t id = 1; id < outGraph.repository.size(); id++) {
      if (id % 1000000 == 0) {
	std::cerr << "Processed " << id << std::endl;
      }
      try {
	NGT::GraphNode &node = *outGraph.getNode(id);
	if (node.size() == 0) {
	  continue;
	}
	node.clear();
	NGT::GraphNode empty;
	node.swap(empty);
      } catch(NGT::Exception &err) {
	std::cerr << "GraphReconstructor: Warning. Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
	continue;
      }
    }
    NGT::GraphIndex::showStatisticsOfGraph(dynamic_cast<NGT::GraphIndex&>(outIndex.getIndex()));

    std::vector<ObjectDistances> reverse(graph.size() + 1);	
    for (size_t id = 1; id <= graph.size(); ++id) {
      try {
	NGT::GraphNode &node = graph[id - 1];
	if (id % 100000 == 0) {
	  std::cerr << "Processed (summing up) " << id << std::endl;
	}
	for (size_t rank = 0; rank < node.size(); rank++) {
	  reverse[node[rank].id].push_back(ObjectDistance(id, node[rank].distance));
	}
      } catch(NGT::Exception &err) {
	std::cerr << "GraphReconstructor: Warning. Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
	continue;
      }
    }

    std::vector<std::pair<size_t, size_t> > reverseSize(graph.size() + 1);	
    reverseSize[0] = std::pair<size_t, size_t>(0, 0);
    for (size_t rid = 1; rid <= graph.size(); ++rid) {
      reverseSize[rid] = std::pair<size_t, size_t>(reverse[rid].size(), rid);
    }
    std::sort(reverseSize.begin(), reverseSize.end());		


    std::vector<uint32_t> indegreeCount(graph.size(), 0);	
    size_t zeroCount = 0;
    for (size_t sizerank = 0; sizerank <= reverseSize.size(); sizerank++) {
      
      if (reverseSize[sizerank].first == 0) {
	zeroCount++;
	continue;
      }
      size_t rid = reverseSize[sizerank].second;	
      ObjectDistances &rnode = reverse[rid];		
      for (auto rni = rnode.begin(); rni != rnode.end(); ++rni) {
	if (indegreeCount[(*rni).id] >= reverseEdgeSize) {	
	  continue;
	}
	NGT::GraphNode &node = *outGraph.getNode(rid);	
	if (indegreeCount[(*rni).id] > 0 && node.size() >= originalEdgeSize) {
	  continue;
	}
	
	node.push_back(NGT::ObjectDistance((*rni).id, (*rni).distance));
	indegreeCount[(*rni).id]++;
      }
    }
    reverseEdgeTimer.stop();    
    std::cerr << "The number of nodes with zero outdegree by reverse edges=" << zeroCount << std::endl;
    NGT::GraphIndex::showStatisticsOfGraph(dynamic_cast<NGT::GraphIndex&>(outIndex.getIndex()));

    normalizeEdgeTimer.start();    
    for (size_t id = 1; id < outGraph.repository.size(); id++) {
      try {
	NGT::GraphNode &n = *outGraph.getNode(id);
	if (id % 100000 == 0) {
	  std::cerr << "Processed " << id << std::endl;
	}
	std::sort(n.begin(), n.end());
	NGT::ObjectID prev = 0;
	for (auto it = n.begin(); it != n.end();) {
	  if (prev == (*it).id) {
	    it = n.erase(it);
	    continue;
	  }
	  prev = (*it).id;
	  it++;
	}
	NGT::GraphNode tmp = n;
	n.swap(tmp);
      } catch (...) {
	std::cerr << "Graph::construct: error. something wrong. ID=" << id << std::endl;
      }
    }
    normalizeEdgeTimer.stop();
    NGT::GraphIndex::showStatisticsOfGraph(dynamic_cast<NGT::GraphIndex&>(outIndex.getIndex()));

    originalEdgeTimer.start();
    for (size_t id = 1; id < outGraph.repository.size(); id++) {
      if (id % 1000000 == 0) {
	std::cerr << "Processed " << id << std::endl;
      }
      NGT::GraphNode &node = graph[id - 1];
      try {
	NGT::GraphNode &onode = *outGraph.getNode(id);
	bool stop = false;
	for (size_t rank = 0; (rank < node.size() && rank < originalEdgeSize) && stop == false; rank++) {
	  switch (mode) {
	  case 'a':
	    if (onode.size() >= originalEdgeSize) {
	      stop = true;
	      continue;
	    }
	    break;
	  case 'c':
	    break;
	  }
	  NGT::Distance distance = node[rank].distance;
	  size_t nodeID = node[rank].id;
	  outGraph.addEdge(id, nodeID, distance, false);
	}
      } catch(NGT::Exception &err) {
	std::cerr << "GraphReconstructor: Warning. Cannot get the node. ID=" << id << ":" << err.what() << std::endl;
	continue;
      }
    }
    originalEdgeTimer.stop();
    NGT::GraphIndex::showStatisticsOfGraph(dynamic_cast<NGT::GraphIndex&>(outIndex.getIndex()));

    std::cerr << "Reconstruction time=" << originalEdgeTimer.time << ":" << reverseEdgeTimer.time 
	 << ":" << normalizeEdgeTimer.time << std::endl;
    std::cerr << "original edge size=" << originalEdgeSize << std::endl;
    std::cerr << "reverse edge size=" << reverseEdgeSize << std::endl;

#endif
  }


};

}; // NGT
