#pragma once

#include "core/render_context.h"
#include "layer/layer.h"
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
		virtual void Render() = 0;

		// Z-index for render ordering (higher values render on top)
		// Set via Args in derived classes, e.g.: Rectangle::Args{.zIndex = 5}
		short zIndex{0};
	};

	// ============================================================================
	// ILayer - Interface for elements that participate in the update loop
	// ============================================================================
	//
	// Elements that need to handle input or update state implement ILayer.
	// This includes Components (Button, TextInput) and Container.
	// Shapes do NOT implement ILayer - they only render.

	struct ILayer : public IComponent {
		virtual void HandleInput() = 0;
		virtual void Update(float deltaTime) = 0;
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
			: m_buffer(std::make_unique<char[]>(capacity)),
			  m_capacity(capacity),
			  m_offset(0) {}

		~MemoryArena() {
			// Call destructors for all allocated objects
			for (auto& [ptr, destructor] : m_destructors) {
				destructor(ptr);
			}
		}

		// Non-copyable, movable
		MemoryArena(const MemoryArena&) = delete;
		MemoryArena& operator=(const MemoryArena&) = delete;
		MemoryArena(MemoryArena&&) = default;
		MemoryArena& operator=(MemoryArena&&) = default;

		template <typename T, typename... Args>
		T* Allocate(Args&&... args) {
			// Align to type's alignment requirement
			size_t alignment = alignof(T);
			size_t alignedOffset = (m_offset + alignment - 1) & ~(alignment - 1);
			size_t requiredSize = alignedOffset + sizeof(T);

			// Arena is non-growable - memcpy would break vtables for polymorphic types
			// If you hit this, increase the arena capacity at construction
			if (requiredSize > m_capacity) {
				throw std::runtime_error("MemoryArena capacity exceeded. Increase initial capacity.");
			}

			// Construct object in place
			T* ptr = new (m_buffer.get() + alignedOffset) T(std::forward<Args>(args)...);
			m_offset = alignedOffset + sizeof(T);

			// Track destructor for cleanup
			m_destructors.emplace_back(ptr, [](void* p) { static_cast<T*>(p)->~T(); });

			return ptr;
		}

		void Clear() {
			// Call destructors and reset
			for (auto& [ptr, destructor] : m_destructors) {
				destructor(ptr);
			}
			m_destructors.clear();
			m_offset = 0;
		}

	  private:
		std::unique_ptr<char[]>						   m_buffer;
		size_t										   m_capacity;
		size_t										   m_offset;
		std::vector<std::pair<void*, void (*)(void*)>> m_destructors;
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
		LayerHandle AddChild(T&& child) {
			static_assert(std::is_base_of_v<IComponent, std::decay_t<T>>, "Child must implement IComponent");

			auto* ptr = m_arena.Allocate<std::decay_t<T>>(std::forward<T>(child));
			m_children.push_back(ptr);
			m_childrenNeedSorting = true;

			uint16_t index = static_cast<uint16_t>(m_children.size() - 1);
			return LayerHandle::Make(index, m_generation);
		}

		// Get child by handle (returns nullptr if invalid)
		template <typename T>
		T* GetChild(LayerHandle handle) {
			if (!handle.IsValid() || handle.GetGeneration() != m_generation) {
				return nullptr;
			}
			uint16_t index = handle.GetIndex();
			if (index >= m_children.size()) {
				return nullptr;
			}
			return dynamic_cast<T*>(m_children[index]);
		}

		// ILayer implementation - propagates to children
		void HandleInput() override {
			for (auto* child : m_children) {
				if (auto* layer = dynamic_cast<ILayer*>(child)) {
					layer->HandleInput();
				}
			}
		}

		void Update(float deltaTime) override {
			for (auto* child : m_children) {
				if (auto* layer = dynamic_cast<ILayer*>(child)) {
					layer->Update(deltaTime);
				}
			}
		}

		void Render() override {
			// Sort children by zIndex when needed (stable sort preserves insertion order for equal zIndex)
			if (m_childrenNeedSorting) {
				std::stable_sort(m_children.begin(), m_children.end(), [](const IComponent* a, const IComponent* b) {
					return a->zIndex < b->zIndex;
				});
				m_childrenNeedSorting = false;
			}

			for (auto* child : m_children) {
				RenderContext::SetZIndex(child->zIndex);
				child->Render();
			}
		}

		// Mark children for re-sort (call when a child's zIndex changes)
		void MarkChildrenNeedSorting() { m_childrenNeedSorting = true; }

	  protected:
		MemoryArena				 m_arena;
		std::vector<IComponent*> m_children;
		uint16_t				 m_generation{0};
		bool					 m_childrenNeedSorting{false};
	};

} // namespace UI
