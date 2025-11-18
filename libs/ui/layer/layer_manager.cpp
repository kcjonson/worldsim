#include "layer/layer_manager.h"
#include "shapes/shapes.h"
#include <algorithm>
#include <cassert>

namespace ui {

	// --- Layer Creation ---

	uint32_t LayerManager::CreateRectangle(const Rectangle& rect) {
		LayerNode newNode{.data = rect, .active = true};
		uint32_t index = m_freeList.empty() ? static_cast<uint32_t>(m_nodes.size()) : m_freeList.back();
		if (m_freeList.empty()) {
			m_nodes.push_back(newNode);
		} else {
			m_freeList.pop_back();
			m_nodes[index] = newNode;
		}
		return index;
	}

	uint32_t LayerManager::CreateCircle(const Circle& circle) {
		LayerNode newNode{.data = circle, .active = true};
		uint32_t index = m_freeList.empty() ? static_cast<uint32_t>(m_nodes.size()) : m_freeList.back();
		if (m_freeList.empty()) {
			m_nodes.push_back(newNode);
		} else {
			m_freeList.pop_back();
			m_nodes[index] = newNode;
		}
		return index;
	}

	uint32_t LayerManager::CreateText(const Text& text) {
		LayerNode newNode{.data = text, .active = true};
		uint32_t index = m_freeList.empty() ? static_cast<uint32_t>(m_nodes.size()) : m_freeList.back();
		if (m_freeList.empty()) {
			m_nodes.push_back(newNode);
		} else {
			m_freeList.pop_back();
			m_nodes[index] = newNode;
		}
		return index;
	}

	uint32_t LayerManager::CreateLine(const Line& line) {
		LayerNode newNode{.data = line, .active = true};
		uint32_t index = m_freeList.empty() ? static_cast<uint32_t>(m_nodes.size()) : m_freeList.back();
		if (m_freeList.empty()) {
			m_nodes.push_back(newNode);
		} else {
			m_freeList.pop_back();
			m_nodes[index] = newNode;
		}
		return index;
	}

	// --- Hierarchy Management ---

	void LayerManager::AddChild(uint32_t parentIndex, uint32_t childIndex) {
		assert(IsValidIndex(parentIndex));
		assert(IsValidIndex(childIndex));
		assert(parentIndex != childIndex);
		assert(!IsAncestor(childIndex, parentIndex));

		LayerNode& parent = m_nodes[parentIndex];
		LayerNode& child = m_nodes[childIndex];

		// Remove child from old parent if it has one
		if (!child.IsRoot()) {
			RemoveChild(child.parentIndex, childIndex);
		}

		// Add to new parent
		parent.childIndices.push_back(childIndex);
		child.parentIndex = parentIndex;

		// Mark parent as needing sort (dirty flag optimization)
		// Only mark if adding breaks sort order (optimization from colonysim)
		if (!parent.childrenNeedSorting && !parent.childIndices.empty()) {
			// If new child's zIndex < last child's zIndex, we need to sort
			if (parent.childIndices.size() > 1) {
				uint32_t lastChildIndex = parent.childIndices[parent.childIndices.size() - 2];
				if (child.zIndex < m_nodes[lastChildIndex].zIndex) {
					parent.childrenNeedSorting = true;
				}
			}
		}
	}

	void LayerManager::RemoveChild(uint32_t parentIndex, uint32_t childIndex) {
		assert(IsValidIndex(parentIndex));
		assert(IsValidIndex(childIndex));

		LayerNode& parent = m_nodes[parentIndex];
		LayerNode& child = m_nodes[childIndex];

		// Find and remove child from parent's list
		auto it = std::find(parent.childIndices.begin(), parent.childIndices.end(), childIndex);
		if (it != parent.childIndices.end()) {
			parent.childIndices.erase(it);
			child.parentIndex = UINT32_MAX; // Mark as root
		}

		// Note: Removing doesn't break sort order, so no need to set dirty flag
	}

	const std::vector<uint32_t>& LayerManager::GetChildren(uint32_t nodeIndex) const {
		assert(IsValidIndex(nodeIndex));
		return m_nodes[nodeIndex].childIndices;
	}

	// --- Z-Index Management ---

	void LayerManager::SetZIndex(uint32_t nodeIndex, float zIndex) {
		assert(IsValidIndex(nodeIndex));

		LayerNode& node = m_nodes[nodeIndex];

		if (node.zIndex != zIndex) {
			node.zIndex = zIndex;

			// Mark parent as needing sort (dirty flag optimization from colonysim)
			if (!node.IsRoot()) {
				m_nodes[node.parentIndex].childrenNeedSorting = true;
			}
		}
	}

	float LayerManager::GetZIndex(uint32_t nodeIndex) const {
		assert(IsValidIndex(nodeIndex));
		return m_nodes[nodeIndex].zIndex;
	}

	void LayerManager::SortChildren(uint32_t nodeIndex) {
		assert(IsValidIndex(nodeIndex));

		LayerNode& node = m_nodes[nodeIndex];

		// Only sort if dirty flag is set (performance optimization)
		if (node.childrenNeedSorting) {
			std::sort(node.childIndices.begin(), node.childIndices.end(), [this](uint32_t a, uint32_t b) {
				return m_nodes[a].zIndex < m_nodes[b].zIndex;
			});

			node.childrenNeedSorting = false;
		}
	}

	// --- Visibility ---

	void LayerManager::SetVisible(uint32_t nodeIndex, bool visible) {
		assert(IsValidIndex(nodeIndex));
		m_nodes[nodeIndex].visible = visible;
	}

	bool LayerManager::IsVisible(uint32_t nodeIndex) const {
		assert(IsValidIndex(nodeIndex));
		return m_nodes[nodeIndex].visible;
	}

	// --- Access ---

	const LayerNode& LayerManager::GetNode(uint32_t nodeIndex) const {
		assert(IsValidIndex(nodeIndex));
		return m_nodes[nodeIndex];
	}

	LayerNode& LayerManager::GetNode(uint32_t nodeIndex) {
		assert(IsValidIndex(nodeIndex));
		return m_nodes[nodeIndex];
	}

	const LayerData& LayerManager::GetData(uint32_t nodeIndex) const {
		assert(IsValidIndex(nodeIndex));
		return m_nodes[nodeIndex].data;
	}

	LayerData& LayerManager::GetData(uint32_t nodeIndex) {
		assert(IsValidIndex(nodeIndex));
		return m_nodes[nodeIndex].data;
	}

	// --- Rendering ---

	void LayerManager::RenderAll() {
		// Render all active root nodes (nodes without parents)
		for (uint32_t i = 0; i < m_nodes.size(); ++i) {
			if (m_nodes[i].active && m_nodes[i].IsRoot()) {
				RenderNode(i);
			}
		}
	}

	void LayerManager::RenderSubtree(uint32_t rootIndex) {
		assert(IsValidIndex(rootIndex));
		RenderNode(rootIndex);
	}

	void LayerManager::RenderNode(uint32_t nodeIndex) {
		LayerNode& node = m_nodes[nodeIndex];

		// Skip if not visible
		if (!node.visible) {
			return;
		}

		// Sort children if needed (dirty flag optimization)
		SortChildren(nodeIndex);

		// Render this node using std::visit pattern
		std::visit([](auto& shape) { shape.Render(); }, node.data);

		// Recursively render children in z-order
		for (uint32_t childIndex : node.childIndices) {
			RenderNode(childIndex);
		}
	}

	// --- Update ---

	void LayerManager::UpdateAll(float deltaTime) {
		// Update all active root nodes
		for (uint32_t i = 0; i < m_nodes.size(); ++i) {
			if (m_nodes[i].active && m_nodes[i].IsRoot()) {
				UpdateNode(i, deltaTime);
			}
		}
	}

	void LayerManager::UpdateSubtree(uint32_t rootIndex, float deltaTime) {
		assert(IsValidIndex(rootIndex));
		UpdateNode(rootIndex, deltaTime);
	}

	void LayerManager::UpdateNode(uint32_t nodeIndex, float deltaTime) {
		LayerNode& node = m_nodes[nodeIndex];

		// Skip if not visible
		if (!node.visible) {
			return;
		}

		// TODO: Add update logic for shapes (animations, etc.)
		// For now, shapes are static, so nothing to update

		// Recursively update children
		for (uint32_t childIndex : node.childIndices) {
			UpdateNode(childIndex, deltaTime);
		}
	}

	// --- Lifecycle ---

	// DestroyLayer is the public API for destroying a layer and its children.
	// Responsibilities:
	//   1. Remove the node from its parent (if it has one)
	//   2. Recursively destroy the entire subtree (via DestroySubtree)
	//   3. Add the root node to the free list
	//
	// DestroySubtree is the internal recursive helper.
	// Responsibilities:
	//   1. Recursively destroy all children
	//   2. Add children to the free list
	//   3. Clear the node's data and mark it as inactive
	//   4. Does NOT add the root node to the free list (caller's responsibility)
	void LayerManager::DestroyLayer(uint32_t nodeIndex) {
		assert(IsValidIndex(nodeIndex));

		// Remove from parent if it has one
		LayerNode& node = m_nodes[nodeIndex];
		if (!node.IsRoot()) {
			RemoveChild(node.parentIndex, nodeIndex);
		}

		// Recursively destroy all children
		DestroySubtree(nodeIndex);

		// Add to free list for reuse
		m_freeList.push_back(nodeIndex);
	}

	void LayerManager::DestroySubtree(uint32_t nodeIndex) {
		LayerNode& node = m_nodes[nodeIndex];

		// Copy child indices (we'll modify the vector during iteration)
		std::vector<uint32_t> childrenCopy = node.childIndices;

		// Recursively destroy children
		for (uint32_t childIndex : childrenCopy) {
			DestroySubtree(childIndex);
			m_freeList.push_back(childIndex);
		}

		// Clear this node's data and mark as inactive
		node.childIndices.clear();
		node.parentIndex = UINT32_MAX;
		node.visible = true;
		node.active = false; // Mark as inactive so RenderAll/UpdateAll skip it
		node.zIndex = 0.0F;
		node.childrenNeedSorting = false;
	}

	void LayerManager::Clear() {
		m_nodes.clear();
		m_freeList.clear();
	}

	// --- Internal Helpers ---

	bool LayerManager::IsValidIndex(uint32_t index) const {
		return index < m_nodes.size();
	}

	bool LayerManager::IsAncestor(uint32_t ancestor, uint32_t node) const {
		// Walk up the parent chain from 'node' to see if we reach 'ancestor'
		uint32_t current = node;
		while (!m_nodes[current].IsRoot()) {
			current = m_nodes[current].parentIndex;
			if (current == ancestor) {
				return true; // Found ancestor in parent chain
			}
		}
		return false; // Reached root without finding ancestor
	}

} // namespace ui
