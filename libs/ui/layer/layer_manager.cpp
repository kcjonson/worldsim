#include "layer/layer_manager.h"
#include "shapes/shapes.h"
#include <algorithm>
#include <cassert>

namespace UI {

	// --- Layer Creation ---

	uint32_t LayerManager::CreateRectangle(const Rectangle& rect) {
		uint32_t index;

		// Reuse from free list if available
		if (!m_freeList.empty()) {
			index = m_freeList.back();
			m_freeList.pop_back();
			m_nodes[index] = LayerNode{.data = rect};
		} else {
			index = static_cast<uint32_t>(m_nodes.size());
			m_nodes.push_back(LayerNode{.data = rect});
		}

		return index;
	}

	uint32_t LayerManager::CreateCircle(const Circle& circle) {
		uint32_t index;

		if (!m_freeList.empty()) {
			index = m_freeList.back();
			m_freeList.pop_back();
			m_nodes[index] = LayerNode{.data = circle};
		} else {
			index = static_cast<uint32_t>(m_nodes.size());
			m_nodes.push_back(LayerNode{.data = circle});
		}

		return index;
	}

	uint32_t LayerManager::CreateText(const Text& text) {
		uint32_t index;

		if (!m_freeList.empty()) {
			index = m_freeList.back();
			m_freeList.pop_back();
			m_nodes[index] = LayerNode{.data = text};
		} else {
			index = static_cast<uint32_t>(m_nodes.size());
			m_nodes.push_back(LayerNode{.data = text});
		}

		return index;
	}

	uint32_t LayerManager::CreateLine(const Line& line) {
		uint32_t index;

		if (!m_freeList.empty()) {
			index = m_freeList.back();
			m_freeList.pop_back();
			m_nodes[index] = LayerNode{.data = line};
		} else {
			index = static_cast<uint32_t>(m_nodes.size());
			m_nodes.push_back(LayerNode{.data = line});
		}

		return index;
	}

	// --- Hierarchy Management ---

	void LayerManager::AddChild(uint32_t parentIndex, uint32_t childIndex) {
		assert(IsValidIndex(parentIndex) && "Parent index out of range");
		assert(IsValidIndex(childIndex) && "Child index out of range");
		assert(parentIndex != childIndex && "Cannot add layer as its own child");

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
		assert(IsValidIndex(parentIndex) && "Parent index out of range");
		assert(IsValidIndex(childIndex) && "Child index out of range");

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
		assert(IsValidIndex(nodeIndex) && "Node index out of range");
		return m_nodes[nodeIndex].childIndices;
	}

	// --- Z-Index Management ---

	void LayerManager::SetZIndex(uint32_t nodeIndex, float zIndex) {
		assert(IsValidIndex(nodeIndex) && "Node index out of range");

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
		assert(IsValidIndex(nodeIndex) && "Node index out of range");
		return m_nodes[nodeIndex].zIndex;
	}

	void LayerManager::SortChildren(uint32_t nodeIndex) {
		assert(IsValidIndex(nodeIndex) && "Node index out of range");

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
		assert(IsValidIndex(nodeIndex) && "Node index out of range");
		m_nodes[nodeIndex].visible = visible;
	}

	bool LayerManager::IsVisible(uint32_t nodeIndex) const {
		assert(IsValidIndex(nodeIndex) && "Node index out of range");
		return m_nodes[nodeIndex].visible;
	}

	// --- Access ---

	const LayerNode& LayerManager::GetNode(uint32_t nodeIndex) const {
		assert(IsValidIndex(nodeIndex) && "Node index out of range");
		return m_nodes[nodeIndex];
	}

	LayerNode& LayerManager::GetNode(uint32_t nodeIndex) {
		assert(IsValidIndex(nodeIndex) && "Node index out of range");
		return m_nodes[nodeIndex];
	}

	const LayerData& LayerManager::GetData(uint32_t nodeIndex) const {
		assert(IsValidIndex(nodeIndex) && "Node index out of range");
		return m_nodes[nodeIndex].data;
	}

	LayerData& LayerManager::GetData(uint32_t nodeIndex) {
		assert(IsValidIndex(nodeIndex) && "Node index out of range");
		return m_nodes[nodeIndex].data;
	}

	// --- Rendering ---

	void LayerManager::RenderAll() {
		// Render all root nodes (nodes without parents)
		for (uint32_t i = 0; i < m_nodes.size(); ++i) {
			if (m_nodes[i].IsRoot()) {
				RenderNode(i);
			}
		}
	}

	void LayerManager::RenderSubtree(uint32_t rootIndex) {
		assert(IsValidIndex(rootIndex) && "Root index out of range");
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
		// Update all root nodes
		for (uint32_t i = 0; i < m_nodes.size(); ++i) {
			if (m_nodes[i].IsRoot()) {
				UpdateNode(i, deltaTime);
			}
		}
	}

	void LayerManager::UpdateSubtree(uint32_t rootIndex, float deltaTime) {
		assert(IsValidIndex(rootIndex) && "Root index out of range");
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

	void LayerManager::DestroyLayer(uint32_t nodeIndex) {
		assert(IsValidIndex(nodeIndex) && "Node index out of range");

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

		// Clear this node's data
		node.childIndices.clear();
		node.parentIndex = UINT32_MAX;
		node.visible = true;
		node.zIndex = 0.0f;
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

} // namespace UI
