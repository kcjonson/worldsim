#include "DependencyGraph.h"

namespace engine::assets {

	void DependencyGraph::addNode(const std::string& node) {
		m_nodes.insert(node);
	}

	void DependencyGraph::addDependency(const std::string& dependent, const std::string& dependency) {
		// Add both nodes to the graph
		m_nodes.insert(dependent);
		m_nodes.insert(dependency);

		// Add edge: dependent → dependency (dependent requires dependency)
		m_edges[dependent].insert(dependency);
	}

	std::vector<std::string> DependencyGraph::getSpawnOrder() const {
		std::unordered_set<std::string> visited;
		std::unordered_set<std::string> inStack;
		std::vector<std::string>		order;
		order.reserve(m_nodes.size());

		// Process all nodes (some may have no dependencies)
		for (const auto& node : m_nodes) {
			if (visited.find(node) == visited.end()) {
				if (!dfs(node, visited, inStack, order)) {
					throw CyclicDependencyError("Circular dependency detected in entity placement");
				}
			}
		}

		// DFS naturally produces topological order: nodes are added
		// after all their dependencies have been processed
		return order;
	}

	bool DependencyGraph::hasCycle() const {
		std::unordered_set<std::string> visited;
		std::unordered_set<std::string> inStack;
		std::vector<std::string>		order;

		for (const auto& node : m_nodes) {
			if (visited.find(node) == visited.end()) {
				if (!dfs(node, visited, inStack, order)) {
					return true;
				}
			}
		}
		return false;
	}

	std::vector<std::string> DependencyGraph::getDependencies(const std::string& node) const {
		auto it = m_edges.find(node);
		if (it == m_edges.end()) {
			return {};
		}
		return {it->second.begin(), it->second.end()};
	}

	void DependencyGraph::clear() {
		m_nodes.clear();
		m_edges.clear();
	}

	bool DependencyGraph::dfs(const std::string& node,
							  std::unordered_set<std::string>& visited,
							  std::unordered_set<std::string>& inStack,
							  std::vector<std::string>& order) const {
		// Mark node as being processed
		inStack.insert(node);

		// Process all dependencies
		auto edgesIt = m_edges.find(node);
		if (edgesIt != m_edges.end()) {
			for (const auto& dependency : edgesIt->second) {
				// If dependency is in current stack, we have a cycle
				if (inStack.find(dependency) != inStack.end()) {
					return false;
				}

				// If not visited, recurse
				if (visited.find(dependency) == visited.end()) {
					if (!dfs(dependency, visited, inStack, order)) {
						return false;
					}
				}
			}
		}

		// Done processing this node
		inStack.erase(node);
		visited.insert(node);
		order.push_back(node);

		return true;
	}

} // namespace engine::assets
