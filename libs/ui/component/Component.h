#pragma once

#include "core/RenderContext.h"
#include "input/InputEvent.h"
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

		/// Handle an input event. Return true if the event was consumed.
		/// Override in interactive components to handle mouse events.
		/// Call event.consume() to stop propagation to lower z-index components.
		virtual bool handleEvent(InputEvent& /*event*/) { return false; }

		/// Check if a point (in screen coordinates) is within this component's bounds.
		/// Override in interactive components to enable hit testing.
		virtual bool containsPoint(Foundation::Vec2 /*point*/) const { return false; }

		// ========== Layout API ==========
		// These methods enable automatic layout via LayoutContainer.
		// Size getters return the element's size INCLUDING margin.
		// setPosition is called by layout containers to position the element.

		/// Get total width including margin (content width + margin * 2)
		virtual float getWidth() const = 0;

		/// Get total height including margin (content height + margin * 2)
		virtual float getHeight() const = 0;

		/// Set position (called by layout containers)
		/// The element should render its content at position + margin
		virtual void setPosition(float x, float y) = 0;

		// Margin (CSS-like): adds space around the element
		// - Reported size includes margin (getWidth/getHeight)
		// - Content renders at position + margin
		float margin{0.0F};

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
		// Position and size for layout
		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{0.0F, 0.0F};

		Component() = default;
		virtual ~Component() = default;

		// ========== IComponent Layout API Implementation ==========

		/// Width including margin
		float getWidth() const override { return size.x + margin * 2.0F; }

		/// Height including margin
		float getHeight() const override { return size.y + margin * 2.0F; }

		/// Set position (layout containers call this)
		void setPosition(float x, float y) override { position = {x, y}; }

		/// Helper: get content position (position + margin) for rendering
		[[nodiscard]] Foundation::Vec2 getContentPosition() const { return {position.x + margin, position.y + margin}; }

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

		// Get child by handle (const version)
		template <typename T>
		const T* getChild(LayerHandle handle) const {
			if (!handle.isValid() || handle.getGeneration() != generation) {
				return nullptr;
			}
			uint16_t index = handle.getIndex();
			if (index >= children.size()) {
				return nullptr;
			}
			return dynamic_cast<const T*>(children[index]);
		}

		// ILayer implementation - propagates to children (skips invisible)
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

		/// Dispatch an event to children in z-order (highest first).
		/// Returns true if any child consumed the event.
		/// This is the core of the event system - call this from containers
		/// instead of manually delegating to each child.
		bool dispatchEvent(InputEvent& event) {
			// Ensure children are sorted by zIndex
			if (childrenNeedSorting) {
				std::stable_sort(children.begin(), children.end(), [](const IComponent* a, const IComponent* b) {
					return a->zIndex < b->zIndex;
				});
				childrenNeedSorting = false;
			}

			// Dispatch in reverse order (highest zIndex first)
			for (auto it = children.rbegin(); it != children.rend(); ++it) {
				IComponent* child = *it;
				if (!child->visible) {
					continue;
				}

				// Let the child handle the event
				if (child->handleEvent(event)) {
					return true;
				}

				// Short-circuit if event was consumed
				if (event.isConsumed()) {
					return true;
				}
			}
			return false;
		}

	  protected:
		MemoryArena				 arena;
		std::vector<IComponent*> children;
		Foundation::Rect		 bounds;
		uint16_t				 generation{0};
		bool					 childrenNeedSorting{false};
	};

} // namespace UI
