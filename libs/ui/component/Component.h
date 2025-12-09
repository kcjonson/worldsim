#pragma once

#include "core/RenderContext.h"
#include "layer/Layer.h"

#include <graphics/Rect.h>
#include <math/Types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace UI {

	// ============================================================================
	// IComponent - Base interface for all UI elements
	// ============================================================================
	//
	// Everything that can be rendered implements IComponent.
	// Shapes (Rectangle, Circle, Text) implement only IComponent.

	struct IComponent {
		virtual ~IComponent() = default;
		virtual void render() = 0;

		// Z-index for render ordering (higher values render on top)
		// Valid range: -32768 to 32767 (signed 16-bit)
		// Set via Args in derived classes, e.g.: Rectangle::Args{.zIndex = 5}
		short zIndex{0};

		// Visibility flag - when false, this component and all descendants are skipped
		// during render, handleInput, and update. Use this instead of positioning offscreen.
		bool visible{true};
	};

	// ============================================================================
	// ILayer - Interface for elements that participate in the update loop
	// ============================================================================
	//
	// Elements that need to handle input or update state implement ILayer.
	// This includes Components (Button, TextInput) and Container.
	// Shapes do NOT implement ILayer - they only render.

	struct ILayer : public IComponent {
		virtual void handleInput() = 0;
		virtual void update(float deltaTime) = 0;

		/// Called when bounds change. Position children within the given bounds.
		/// @param bounds The available space for this component
		virtual void layout(const Foundation::Rect& bounds) = 0;
	};

	// ============================================================================
	// MemoryArena - Contiguous memory allocator for child components
	// ============================================================================
	//
	// Allocates objects from a contiguous memory block for cache-friendly iteration.
	// Used by Component to store children.
	//
	// NOTE: Arena is non-growable by design. Growing would require memcpy which
	// breaks vtable pointers for polymorphic types. If capacity is exceeded,
	// an exception is thrown - increase the capacity at construction time.

	class MemoryArena {
	  public:
		explicit MemoryArena(size_t capacity = 64 * 1024) // 64KB default
			: buffer(std::make_unique<char[]>(capacity)),
			  capacity(capacity),
			  offset(0) {}

		~MemoryArena() {
			// Call destructors for all allocated objects
			for (auto& [ptr, destructor] : destructors) {
				destructor(ptr);
			}
		}

		// Non-copyable, movable
		MemoryArena(const MemoryArena&) = delete;
		MemoryArena& operator=(const MemoryArena&) = delete;
		MemoryArena(MemoryArena&&) = default;
		MemoryArena& operator=(MemoryArena&&) = default;

		template <typename T, typename... Args>
		T* allocate(Args&&... args) {
			// Align to type's alignment requirement
			size_t alignment = alignof(T);
			size_t alignedOffset = (offset + alignment - 1) & ~(alignment - 1);
			size_t requiredSize = alignedOffset + sizeof(T);

			// Arena is non-growable - memcpy would break vtables for polymorphic types
			// If you hit this, increase the arena capacity at construction
			if (requiredSize > this->capacity) {
				throw std::runtime_error("MemoryArena capacity exceeded. Increase initial capacity.");
			}

			// Construct object in place
			T* ptr = new (buffer.get() + alignedOffset) T(std::forward<Args>(args)...);
			offset = alignedOffset + sizeof(T);

			// Track destructor for cleanup
			destructors.emplace_back(ptr, [](void* p) { static_cast<T*>(p)->~T(); });

			return ptr;
		}

		void clear() {
			// Call destructors and reset
			for (auto& [ptr, destructor] : destructors) {
				destructor(ptr);
			}
			destructors.clear();
			offset = 0;
		}

	  private:
		std::unique_ptr<char[]>						   buffer;
		size_t										   capacity;
		size_t										   offset;
		std::vector<std::pair<void*, void (*)(void*)>> destructors;
	};

	// ============================================================================
	// Component - Base class for UI elements that can have children
	// ============================================================================
	//
	// Provides the AddChild() API for building component trees.
	// Children are stored in a MemoryArena for cache-friendly iteration.
	//
	// Usage:
	//   class MyWidget : public Component {
	//       void Initialize() override {
	//           AddChild(Rectangle{...});
	//           AddChild(Button{...});
	//       }
	//   };

	class Component : public ILayer {
	  public:
		// Position (for manual propagation - derived classes override setPosition if needed)
		Foundation::Vec2 position{0.0F, 0.0F};

		Component() = default;
		virtual ~Component() = default;

		// Non-copyable (owns arena memory)
		Component(const Component&) = delete;
		Component& operator=(const Component&) = delete;

		// Movable
		Component(Component&&) = default;
		Component& operator=(Component&&) = default;

		// Add a child component - returns handle for later access
		template <typename T>
		LayerHandle addChild(T&& child) {
			static_assert(std::is_base_of_v<IComponent, std::decay_t<T>>, "Child must implement IComponent");

			auto* ptr = arena.allocate<std::decay_t<T>>(std::forward<T>(child));
			children.push_back(ptr);
			childrenNeedSorting = true;

			uint16_t index = static_cast<uint16_t>(children.size() - 1);
			return LayerHandle::make(index, generation);
		}

		// Get child by handle (returns nullptr if invalid)
		template <typename T>
		T* getChild(LayerHandle handle) {
			if (!handle.isValid() || handle.getGeneration() != generation) {
				return nullptr;
			}
			uint16_t index = handle.getIndex();
			if (index >= children.size()) {
				return nullptr;
			}
			return dynamic_cast<T*>(children[index]);
		}

		// ILayer implementation - propagates to children (skips invisible)
		void handleInput() override {
			for (auto* child : children) {
				if (!child->visible) {
					continue;
				}
				if (auto* layer = dynamic_cast<ILayer*>(child)) {
					layer->handleInput();
				}
			}
		}

		void update(float deltaTime) override {
			for (auto* child : children) {
				if (!child->visible) {
					continue;
				}
				if (auto* layer = dynamic_cast<ILayer*>(child)) {
					layer->update(deltaTime);
				}
			}
		}

		void layout(const Foundation::Rect& newBounds) override {
			bounds = newBounds;
			for (auto* child : children) {
				if (auto* layer = dynamic_cast<ILayer*>(child)) {
					layer->layout(newBounds);
				}
			}
		}

		void render() override {
			// Sort children by zIndex when needed (stable sort preserves insertion order for equal zIndex)
			if (childrenNeedSorting) {
				std::stable_sort(children.begin(), children.end(), [](const IComponent* a, const IComponent* b) {
					return a->zIndex < b->zIndex;
				});
				childrenNeedSorting = false;
			}

			for (auto* child : children) {
				if (!child->visible) {
					continue;
				}
				RenderContext::setZIndex(child->zIndex);
				child->render();
			}
		}

		// Mark children for re-sort (call when a child's zIndex changes)
		void markChildrenNeedSorting() { childrenNeedSorting = true; }

	  protected:
		MemoryArena				 arena;
		std::vector<IComponent*> children;
		Foundation::Rect		 bounds;
		uint16_t				 generation{0};
		bool					 childrenNeedSorting{false};
	};

} // namespace UI
