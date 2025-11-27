#pragma once

#include "layer/layer.h"
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
		LayerData				 data;						 // Actual shape data (contiguous)
		std::vector<LayerHandle> childHandles;				 // Handle-based hierarchy (not pointers!)
		float					 zIndex{0.0F};				 // Z-ordering for rendering
		bool					 visible{true};				 // Visibility flag
		bool					 active{true};				 // Is this node active? (false if in free list)
		bool					 childrenNeedSorting{false}; // Dirty flag optimization
		LayerHandle				 parent;					 // Parent node (Invalid() = no parent)
		uint16_t				 generation{0};				 // Generation counter for stale handle detection

		// Helper to check if this is a root node
		[[nodiscard]] bool IsRoot() const { return !parent.IsValid(); }
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
		// Returns handle to the created layer
		LayerHandle Create(const Container& container);
		LayerHandle Create(const Rectangle& rect);
		LayerHandle Create(const Circle& circle);
		LayerHandle Create(const Text& text);
		LayerHandle Create(const Line& line);

		// --- Hierarchy Management ---

		// Add a child layer to a parent
		// Creates the layer and attaches it in one call
		// Reads zIndex and visible from the shape struct
		// Returns handle to the created child layer
		LayerHandle AddChild(LayerHandle parent, const Container& container);
		LayerHandle AddChild(LayerHandle parent, const Rectangle& rect);
		LayerHandle AddChild(LayerHandle parent, const Circle& circle);
		LayerHandle AddChild(LayerHandle parent, const Text& text);
		LayerHandle AddChild(LayerHandle parent, const Line& line);

		// Add an existing child to a parent layer (for advanced use)
		// Both parent and child must be valid handles
		void AddChild(LayerHandle parent, LayerHandle child);

		// Remove a child from a parent layer
		void RemoveChild(LayerHandle parent, LayerHandle child);

		// Get all children of a layer
		[[nodiscard]] const std::vector<LayerHandle>& GetChildren(LayerHandle handle) const;

		// --- Z-Index Management ---

		// Set z-index for a layer
		// Marks parent as needing sort (dirty flag optimization)
		void SetZIndex(LayerHandle handle, float zIndex);

		// Get z-index for a layer
		[[nodiscard]] float GetZIndex(LayerHandle handle) const;

		// Sort children by z-index (only if dirty flag is set)
		// Called automatically during rendering
		void SortChildren(LayerHandle handle);

		// --- Visibility ---

		// Set visibility for a layer
		void SetVisible(LayerHandle handle, bool visible);

		// Get visibility for a layer
		[[nodiscard]] bool IsVisible(LayerHandle handle) const;

		// --- Access ---

		// Get a layer node by handle
		[[nodiscard]] const LayerNode& GetNode(LayerHandle handle) const;
		[[nodiscard]] LayerNode&	   GetNode(LayerHandle handle);

		// Get layer data by handle (returns variant)
		[[nodiscard]] const LayerData& GetData(LayerHandle handle) const;
		[[nodiscard]] LayerData&	   GetData(LayerHandle handle);

		// Get total number of layers (including inactive slots)
		[[nodiscard]] size_t GetLayerCount() const { return m_nodes.size(); }

		// Check if a handle is valid (correct generation, active slot)
		[[nodiscard]] bool IsValidHandle(LayerHandle handle) const;

		// --- Rendering ---

		// Render all visible layers in z-order
		void RenderAll();

		// Render a specific subtree
		void RenderSubtree(LayerHandle root);

		// --- Update ---

		// Update all layers (for animations, etc.)
		void UpdateAll(float deltaTime);

		// Update a specific subtree
		void UpdateSubtree(LayerHandle root, float deltaTime);

		// --- Lifecycle ---

		// Destroy a layer and all its children
		// Updates parent's child list automatically
		void DestroyLayer(LayerHandle handle);

		// Clear all layers
		void Clear();

	  private:
		// All layer nodes stored contiguously (cache-friendly!)
		std::vector<LayerNode> m_nodes;

		// Free list for reusing destroyed layer indices
		std::vector<uint32_t> m_freeList;

		// Auto-incrementing zIndex for insertion order
		// When shape.zIndex < 0.0F (default is -1.0F), assign this value and increment
		float m_nextAutoZIndex{1.0F};

		// --- Internal Helpers ---

		// Check if 'ancestor' is an ancestor of 'node' (prevents cycles)
		// Uses raw indices internally for efficiency
		[[nodiscard]] bool IsAncestor(LayerHandle ancestor, LayerHandle node) const;

		// Create a layer with the given shape data (handles free list reuse)
		// Returns LayerHandle with generation tracking
		template <typename T>
		LayerHandle CreateLayer(const T& shapeData);

		// Render a single node (recursively renders children)
		void RenderNode(LayerHandle handle);

		// Update a single node (recursively updates children)
		void UpdateNode(LayerHandle handle, float deltaTime);

		// Recursively destroy a subtree
		void DestroySubtree(LayerHandle handle);
	};

} // namespace UI
