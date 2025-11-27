#include "layer/layer_manager.h"
#include "shapes/shapes.h"
#include "core/render_context.h"
#include "font/text_batch_renderer.h"
#include "primitives/primitives.h"
#include <algorithm>
#include <cassert>

namespace UI {

	// --- Handle Validation ---

	bool LayerManager::IsValidHandle(LayerHandle handle) const {
		if (!handle.IsValid()) {
			return false;
		}
		uint16_t index = handle.GetIndex();
		if (index >= m_nodes.size()) {
			return false;
		}
		if (!m_nodes[index].active) {
			return false;
		}
		return m_nodes[index].generation == handle.GetGeneration();
	}

	// --- Internal Helpers ---

	template <typename T>
	LayerHandle LayerManager::CreateLayer(const T& shapeData) {
		uint32_t index;
		uint16_t generation;

		// Auto-assign zIndex if not explicitly set (default is -1.0F)
		// This maintains insertion order by default
		float assignedZIndex = shapeData.zIndex;
		if (assignedZIndex < 0.0F) {
			assignedZIndex = m_nextAutoZIndex;
			m_nextAutoZIndex += 1.0F;
		}

		// Reuse from free list if available
		if (!m_freeList.empty()) {
			index = m_freeList.back();
			m_freeList.pop_back();
			// Increment generation for reused slot (stale handle detection)
			generation = static_cast<uint16_t>(m_nodes[index].generation + 1);
			m_nodes[index] = LayerNode{
				.data = shapeData,
				.zIndex = assignedZIndex,	  // Auto-assigned or explicit
				.visible = shapeData.visible, // Read from shape
				.active = true,
				.generation = generation
			};
		} else {
			index = static_cast<uint32_t>(m_nodes.size());
			generation = 0;
			m_nodes.push_back(
				LayerNode{
					.data = shapeData,
					.zIndex = assignedZIndex,	 // Auto-assigned or explicit
					.visible = shapeData.visible, // Read from shape
					.active = true,
					.generation = generation
				}
			);
		}

		return LayerHandle::Make(static_cast<uint16_t>(index), generation);
	}

	// --- Layer Creation ---

	LayerHandle LayerManager::Create(const Container& container) {
		return CreateLayer(container);
	}

	LayerHandle LayerManager::Create(const Rectangle& rect) {
		return CreateLayer(rect);
	}

	LayerHandle LayerManager::Create(const Circle& circle) {
		return CreateLayer(circle);
	}

	LayerHandle LayerManager::Create(const Text& text) {
		return CreateLayer(text);
	}

	LayerHandle LayerManager::Create(const Line& line) {
		return CreateLayer(line);
	}

	// --- Hierarchy Management ---

	// Convenience overloads - create and attach in one call
	LayerHandle LayerManager::AddChild(LayerHandle parent, const Container& container) {
		LayerHandle child = CreateLayer(container);
		AddChild(parent, child);
		return child;
	}

	LayerHandle LayerManager::AddChild(LayerHandle parent, const Rectangle& rect) {
		LayerHandle child = CreateLayer(rect);
		AddChild(parent, child);
		return child;
	}

	LayerHandle LayerManager::AddChild(LayerHandle parent, const Circle& circle) {
		LayerHandle child = CreateLayer(circle);
		AddChild(parent, child);
		return child;
	}

	LayerHandle LayerManager::AddChild(LayerHandle parent, const Text& text) {
		LayerHandle child = CreateLayer(text);
		AddChild(parent, child);
		return child;
	}

	LayerHandle LayerManager::AddChild(LayerHandle parent, const Line& line) {
		LayerHandle child = CreateLayer(line);
		AddChild(parent, child);
		return child;
	}

	// Add existing child to parent
	void LayerManager::AddChild(LayerHandle parent, LayerHandle child) {
		assert(IsValidHandle(parent) && "Invalid parent handle");
		assert(IsValidHandle(child) && "Invalid child handle");
		assert(parent != child && "Cannot add layer as its own child");
		assert(!IsAncestor(child, parent) && "Cannot add ancestor as child (would create cycle)");

		LayerNode& parentNode = m_nodes[parent.GetIndex()];
		LayerNode& childNode = m_nodes[child.GetIndex()];

		// Remove child from old parent if it has one
		if (!childNode.IsRoot()) {
			RemoveChild(childNode.parent, child);
		}

		// Add to new parent
		parentNode.childHandles.push_back(child);
		childNode.parent = parent;

		// Mark parent as needing sort (dirty flag optimization)
		// Only mark if adding breaks sort order (optimization from colonysim)
		if (!parentNode.childrenNeedSorting && !parentNode.childHandles.empty()) {
			// If new child's zIndex < last child's zIndex, we need to sort
			if (parentNode.childHandles.size() > 1) {
				LayerHandle lastChild = parentNode.childHandles[parentNode.childHandles.size() - 2];
				if (childNode.zIndex < m_nodes[lastChild.GetIndex()].zIndex) {
					parentNode.childrenNeedSorting = true;
				}
			}
		}
	}

	void LayerManager::RemoveChild(LayerHandle parent, LayerHandle child) {
		assert(IsValidHandle(parent) && "Invalid parent handle");
		assert(IsValidHandle(child) && "Invalid child handle");

		LayerNode& parentNode = m_nodes[parent.GetIndex()];
		LayerNode& childNode = m_nodes[child.GetIndex()];

		// Find and remove child from parent's list
		auto it = std::find(parentNode.childHandles.begin(), parentNode.childHandles.end(), child);
		if (it != parentNode.childHandles.end()) {
			parentNode.childHandles.erase(it);
			childNode.parent = LayerHandle::Invalid(); // Mark as root
		}

		// Note: Removing doesn't break sort order, so no need to set dirty flag
	}

	const std::vector<LayerHandle>& LayerManager::GetChildren(LayerHandle handle) const {
		assert(IsValidHandle(handle) && "Invalid handle");
		return m_nodes[handle.GetIndex()].childHandles;
	}

	// --- Z-Index Management ---

	void LayerManager::SetZIndex(LayerHandle handle, float zIndex) {
		assert(IsValidHandle(handle) && "Invalid handle");

		LayerNode& node = m_nodes[handle.GetIndex()];

		if (node.zIndex != zIndex) {
			node.zIndex = zIndex;

			// Mark parent as needing sort (dirty flag optimization from colonysim)
			if (!node.IsRoot()) {
				m_nodes[node.parent.GetIndex()].childrenNeedSorting = true;
			}
		}
	}

	float LayerManager::GetZIndex(LayerHandle handle) const {
		assert(IsValidHandle(handle) && "Invalid handle");
		return m_nodes[handle.GetIndex()].zIndex;
	}

	void LayerManager::SortChildren(LayerHandle handle) {
		assert(IsValidHandle(handle) && "Invalid handle");

		LayerNode& node = m_nodes[handle.GetIndex()];

		// Only sort if dirty flag is set (performance optimization)
		if (node.childrenNeedSorting) {
			// Use stable_sort to preserve insertion order for equal zIndex (like CSS)
			std::stable_sort(node.childHandles.begin(), node.childHandles.end(),
				[this](LayerHandle a, LayerHandle b) {
					return m_nodes[a.GetIndex()].zIndex < m_nodes[b.GetIndex()].zIndex;
				});

			node.childrenNeedSorting = false;
		}
	}

	// --- Visibility ---

	void LayerManager::SetVisible(LayerHandle handle, bool visible) {
		assert(IsValidHandle(handle) && "Invalid handle");
		m_nodes[handle.GetIndex()].visible = visible;
	}

	bool LayerManager::IsVisible(LayerHandle handle) const {
		assert(IsValidHandle(handle) && "Invalid handle");
		return m_nodes[handle.GetIndex()].visible;
	}

	// --- Access ---

	const LayerNode& LayerManager::GetNode(LayerHandle handle) const {
		assert(IsValidHandle(handle) && "Invalid or stale LayerHandle");
		return m_nodes[handle.GetIndex()];
	}

	LayerNode& LayerManager::GetNode(LayerHandle handle) {
		assert(IsValidHandle(handle) && "Invalid or stale LayerHandle");
		return m_nodes[handle.GetIndex()];
	}

	const LayerData& LayerManager::GetData(LayerHandle handle) const {
		assert(IsValidHandle(handle) && "Invalid or stale LayerHandle");
		return m_nodes[handle.GetIndex()].data;
	}

	LayerData& LayerManager::GetData(LayerHandle handle) {
		assert(IsValidHandle(handle) && "Invalid or stale LayerHandle");
		return m_nodes[handle.GetIndex()].data;
	}

	// --- Rendering ---

	void LayerManager::RenderAll() {
		// Render all active root nodes (nodes without parents)
		for (uint32_t i = 0; i < m_nodes.size(); ++i) {
			if (m_nodes[i].active && m_nodes[i].IsRoot()) {
				LayerHandle handle = LayerHandle::Make(static_cast<uint16_t>(i), m_nodes[i].generation);
				RenderNode(handle);
			}
		}

		// Note: Text flushing is handled by the overlay renderer in main.cpp
		// to ensure proper z-ordering across all UI elements including the menu
	}

	void LayerManager::RenderSubtree(LayerHandle root) {
		assert(IsValidHandle(root) && "Invalid root handle");
		RenderNode(root);

		// Note: Text flushing is handled by the overlay renderer in main.cpp
		// to ensure proper z-ordering across all UI elements including the menu
	}

	void LayerManager::RenderNode(LayerHandle handle) {
		LayerNode& node = m_nodes[handle.GetIndex()];

		// Skip if not visible
		if (!node.visible) {
			return;
		}

		// Sort children if needed (dirty flag optimization)
		SortChildren(handle);

		// Set z-index in render context so shapes can access it
		RenderContext::SetZIndex(node.zIndex);

		// Render this node using std::visit pattern
		std::visit([](auto& shape) { shape.Render(); }, node.data);

		// Recursively render children in z-order
		for (LayerHandle childHandle : node.childHandles) {
			RenderNode(childHandle);
		}
	}

	// --- Update ---

	void LayerManager::UpdateAll(float deltaTime) {
		// Update all active root nodes
		for (uint32_t i = 0; i < m_nodes.size(); ++i) {
			if (m_nodes[i].active && m_nodes[i].IsRoot()) {
				LayerHandle handle = LayerHandle::Make(static_cast<uint16_t>(i), m_nodes[i].generation);
				UpdateNode(handle, deltaTime);
			}
		}
	}

	void LayerManager::UpdateSubtree(LayerHandle root, float deltaTime) {
		assert(IsValidHandle(root) && "Invalid root handle");
		UpdateNode(root, deltaTime);
	}

	void LayerManager::UpdateNode(LayerHandle handle, float deltaTime) {
		LayerNode& node = m_nodes[handle.GetIndex()];

		// Skip if not visible
		if (!node.visible) {
			return;
		}

		// TODO: Add update logic for shapes (animations, etc.)
		// For now, shapes are static, so nothing to update

		// Recursively update children
		for (LayerHandle childHandle : node.childHandles) {
			UpdateNode(childHandle, deltaTime);
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
	void LayerManager::DestroyLayer(LayerHandle handle) {
		assert(IsValidHandle(handle) && "Invalid handle");

		uint16_t index = handle.GetIndex();

		// Remove from parent if it has one
		LayerNode& node = m_nodes[index];
		if (!node.IsRoot()) {
			RemoveChild(node.parent, handle);
		}

		// Recursively destroy all children
		DestroySubtree(handle);

		// Add to free list for reuse
		m_freeList.push_back(index);
	}

	void LayerManager::DestroySubtree(LayerHandle handle) {
		uint16_t index = handle.GetIndex();
		LayerNode& node = m_nodes[index];

		// Copy child handles (we'll modify the vector during iteration)
		std::vector<LayerHandle> childrenCopy = node.childHandles;

		// Recursively destroy children
		for (LayerHandle childHandle : childrenCopy) {
			DestroySubtree(childHandle);
			m_freeList.push_back(childHandle.GetIndex());
		}

		// Clear this node's data and mark as inactive
		// Note: generation is preserved - it will be incremented on reuse
		node.childHandles.clear();
		node.parent = LayerHandle::Invalid();
		node.visible = true;
		node.active = false; // Mark as inactive so RenderAll/UpdateAll skip it
		node.zIndex = 0.0f;
		node.childrenNeedSorting = false;
	}

	void LayerManager::Clear() {
		m_nodes.clear();
		m_freeList.clear();
		m_nextAutoZIndex = 1.0F; // Reset auto counter
	}

	// --- Internal Helpers ---

	bool LayerManager::IsAncestor(LayerHandle ancestor, LayerHandle node) const {
		// Walk up the parent chain from 'node' to see if we reach 'ancestor'
		LayerHandle current = node;
		while (!m_nodes[current.GetIndex()].IsRoot()) {
			current = m_nodes[current.GetIndex()].parent;
			if (current == ancestor) {
				return true; // Found ancestor in parent chain
			}
		}
		return false; // Reached root without finding ancestor
	}

	// Explicit template instantiations for all supported shape types
	template LayerHandle LayerManager::CreateLayer(const Container&);
	template LayerHandle LayerManager::CreateLayer(const Rectangle&);
	template LayerHandle LayerManager::CreateLayer(const Circle&);
	template LayerHandle LayerManager::CreateLayer(const Text&);
	template LayerHandle LayerManager::CreateLayer(const Line&);

} // namespace UI
