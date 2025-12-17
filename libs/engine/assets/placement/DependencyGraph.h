#pragma once

// Dependency Graph for Entity Placement
//
// Computes spawn order for entities based on "requires" relationships defined
// in asset XMLs. For example, if mushrooms "require nearby Tree", trees must
// spawn first so the spatial index contains them when mushrooms are placed.
//
// Algorithm: Topological sort via depth-first search (Kahn's algorithm variant)
// - Build graph from asset relationship data
// - DFS from each unvisited node, pushing to stack on backtrack
// - Reverse stack gives dependency-respecting spawn order
// - Cycle detection via "in-stack" tracking during DFS
//
// Example:
//   Mushroom requires Tree → spawn order: [Tree, ..., Mushroom]
//   Moss requires Rock → spawn order: [Rock, ..., Moss]
//   Tree requires nothing → can spawn first
//
// Error Handling:
// - Circular dependencies throw CyclicDependencyError
// - Missing dependencies are silently added as nodes (allows forward refs)
//
// Complexity:
// - Build: O(E) where E = number of dependency edges
// - getSpawnOrder(): O(V + E) where V = number of entity types
//
// Used exclusively by PlacementExecutor::initialize() to compute m_spawnOrder.

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::assets {

	/// Exception thrown when a circular dependency is detected
	class CyclicDependencyError : public std::runtime_error {
	  public:
		explicit CyclicDependencyError(const std::string& message)
			: std::runtime_error(message) {}
	};

	/// Dependency graph for entity spawn ordering.
	/// Entities that "require" others must spawn after their dependencies.
	class DependencyGraph {
	  public:
		DependencyGraph() = default;

		/// Add a node to the graph (entity that may have dependencies)
		void addNode(const std::string& node);

		/// Add a dependency: `dependent` requires `dependency` to spawn first.
		/// Both nodes are automatically added if not present.
		void addDependency(const std::string& dependent, const std::string& dependency);

		/// Returns spawn order (dependencies first).
		/// @throws CyclicDependencyError if circular dependencies exist
		[[nodiscard]] std::vector<std::string> getSpawnOrder() const;

		/// Check if graph has cycles (without throwing)
		[[nodiscard]] bool hasCycle() const;

		/// Get all nodes in the graph
		[[nodiscard]] const std::unordered_set<std::string>& getNodes() const { return m_nodes; }

		/// Get direct dependencies of a node (what it requires)
		[[nodiscard]] std::vector<std::string> getDependencies(const std::string& node) const;

		/// Clear all nodes and edges
		void clear();

	  private:
		std::unordered_set<std::string>									 m_nodes;
		std::unordered_map<std::string, std::unordered_set<std::string>> m_edges; // node → dependencies

		/// DFS helper for topological sort
		/// @param node Current node
		/// @param visited Nodes completely processed
		/// @param inStack Nodes currently in recursion stack (for cycle detection)
		/// @param order Output order (reverse topological)
		/// @returns false if cycle detected
		bool
		dfs(const std::string&				 node,
			std::unordered_set<std::string>& visited,
			std::unordered_set<std::string>& inStack,
			std::vector<std::string>&		 order) const;
	};

} // namespace engine::assets
