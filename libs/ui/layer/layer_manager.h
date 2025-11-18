#pragma once

#include "shapes/shapes.h"
#include <cstdint>
#include <variant>
#include <vector>

// Note: std::variant requires complete types, so we include shapes.h

namespace UI {

	// Type-safe variant for all layer types
	using LayerData = std::variant<Container, Rectangle, Circle, Text, Line>;

	// Layer node in the scene graph hierarchy
	struct LayerNode {
		LayerData			  data;						  // Actual shape data (contiguous)
		std::vector<uint32_t> childIndices;				  // Index-based hierarchy (not pointers!)
		float				  zIndex{0.0F};				  // Z-ordering for rendering
		bool				  visible{true};			  // Visibility flag
		bool				  active{true};				  // Is this node active? (false if in free list)
		bool				  childrenNeedSorting{false}; // Dirty flag optimization
		uint32_t			  parentIndex{UINT32_MAX};	  // Parent node (UINT32_MAX = no parent)

		// Helper to check if this is a root node
		[[nodiscard]] bool IsRoot() const { return parentIndex == UINT32_MAX; }
	};

	// Central manager for all UI layers
	// Owns all layer data in contiguous memory (cache-friendly)
	// Research-aligned: /docs/research/modern_rendering_architecture.md line 82-122
	class LayerManager {
	  public:
		LayerManager() = default;
		~LayerManager() = default;

		// Disable copy, enable move
		LayerManager(const LayerManager&) = delete;
		LayerManager& operator=(const LayerManager&) = delete;
		LayerManager(LayerManager&&) noexcept = default;
		LayerManager& operator=(LayerManager&&) noexcept = default;

		// --- Layer Creation ---

		// Create a standalone layer (no parent)
		// Reads zIndex and visible from the shape struct
		// Returns index to the created layer
		uint32_t Create(const Container& container);
		uint32_t Create(const Rectangle& rect);
		uint32_t Create(const Circle& circle);
		uint32_t Create(const Text& text);
		uint32_t Create(const Line& line);

		// --- Hierarchy Management ---

		// Add a child layer to a parent
		// Creates the layer and attaches it in one call
		// Reads zIndex and visible from the shape struct
		// Returns index to the created child layer
		uint32_t AddChild(uint32_t parentIndex, const Container& container);
		uint32_t AddChild(uint32_t parentIndex, const Rectangle& rect);
		uint32_t AddChild(uint32_t parentIndex, const Circle& circle);
		uint32_t AddChild(uint32_t parentIndex, const Text& text);
		uint32_t AddChild(uint32_t parentIndex, const Line& line);

		// Add an existing child to a parent layer (for advanced use)
		// Both parent and child must be valid indices
		void AddChild(uint32_t parentIndex, uint32_t childIndex);

		// Remove a child from a parent layer
		void RemoveChild(uint32_t parentIndex, uint32_t childIndex);

		// Get all children of a layer
		[[nodiscard]] const std::vector<uint32_t>& GetChildren(uint32_t nodeIndex) const;

		// --- Z-Index Management ---

		// Set z-index for a layer
		// Marks parent as needing sort (dirty flag optimization)
		void SetZIndex(uint32_t nodeIndex, float zIndex);

		// Get z-index for a layer
		[[nodiscard]] float GetZIndex(uint32_t nodeIndex) const;

		// Sort children by z-index (only if dirty flag is set)
		// Called automatically during rendering
		void SortChildren(uint32_t nodeIndex);

		// --- Visibility ---

		// Set visibility for a layer
		void SetVisible(uint32_t nodeIndex, bool visible);

		// Get visibility for a layer
		[[nodiscard]] bool IsVisible(uint32_t nodeIndex) const;

		// --- Access ---

		// Get a layer node by index
		[[nodiscard]] const LayerNode& GetNode(uint32_t nodeIndex) const;
		[[nodiscard]] LayerNode&	   GetNode(uint32_t nodeIndex);

		// Get layer data by index (returns variant)
		[[nodiscard]] const LayerData& GetData(uint32_t nodeIndex) const;
		[[nodiscard]] LayerData&	   GetData(uint32_t nodeIndex);

		// Get total number of layers
		[[nodiscard]] size_t GetLayerCount() const { return m_nodes.size(); }

		// --- Rendering ---

		// Render all visible layers in z-order
		void RenderAll();

		// Render a specific subtree
		void RenderSubtree(uint32_t rootIndex);

		// --- Update ---

		// Update all layers (for animations, etc.)
		void UpdateAll(float deltaTime);

		// Update a specific subtree
		void UpdateSubtree(uint32_t rootIndex, float deltaTime);

		// --- Lifecycle ---

		// Destroy a layer and all its children
		// Updates parent's child list automatically
		void DestroyLayer(uint32_t nodeIndex);

		// Clear all layers
		void Clear();

	  private:
		// All layer nodes stored contiguously (cache-friendly!)
		std::vector<LayerNode> m_nodes;

		// Free list for reusing destroyed layer indices
		std::vector<uint32_t> m_freeList;

		// Auto-incrementing zIndex for insertion order
		// When shape.zIndex == 0.0F (default), assign this value and increment
		float m_nextAutoZIndex{1.0F};

		// --- Internal Helpers ---

		// Validate index is in range
		[[nodiscard]] bool IsValidIndex(uint32_t index) const;

		// Check if 'ancestor' is an ancestor of 'node' (prevents cycles)
		[[nodiscard]] bool IsAncestor(uint32_t ancestor, uint32_t node) const;

		// Create a layer with the given shape data (handles free list reuse)
		template <typename T>
		uint32_t CreateLayer(const T& shapeData);

		// Render a single node (recursively renders children)
		void RenderNode(uint32_t nodeIndex);

		// Update a single node (recursively updates children)
		void UpdateNode(uint32_t nodeIndex, float deltaTime);

		// Recursively destroy a subtree
		void DestroySubtree(uint32_t nodeIndex);
	};

} // namespace UI
