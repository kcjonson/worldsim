#pragma once

#include "EntityID.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <typeindex>
#include <vector>

namespace ecs {

	/// Type-erased base class for component storage
	class IComponentPool {
	  public:
		virtual ~IComponentPool() = default;
		virtual void				 remove(EntityID entity) = 0;
		[[nodiscard]] virtual bool	 has(EntityID entity) const = 0;
		[[nodiscard]] virtual size_t size() const = 0;
	};

	/// Sparse set component storage with O(1) add/remove/has operations.
	/// Uses dense array for cache-friendly iteration.
	///
	/// Lookups take the whole EntityID: a stale id (its index reused by a newer entity) finds no
	/// component, so get is null, has is false and remove does nothing.
	///
	/// Components are stored BY VALUE and relocate when the dense vector grows or
	/// swap-removes. Growth may COPY rather than move (vector reallocation uses
	/// move_if_noexcept, and a component whose implicit move is not noexcept —
	/// any std::unordered_map member does that on MSVC — falls back to copy). So
	/// a component type must never hold pointers or iterators into its own
	/// members: a deep copy has to be self-consistent on its own. Store indices,
	/// not iterators (see Memory's LRU, which crashed the game before that rule).
	template <typename T>
	class ComponentPool : public IComponentPool {
	  public:
		/// Add component to a live entity, returning reference to the new component. The Registry
		/// refuses a destroyed id before it gets here, so an occupied slot is always this entity's.
		template <typename... Args>
		T& add(EntityID entity, Args&&... args) {
			uint32_t index = getIndex(entity);

			// Ensure sparse array is large enough
			if (index >= sparseArray.size()) {
				sparseArray.resize(index + 1, kInvalidIndex);
			}

			// Check if entity already has component
			if (sparseArray[index] != kInvalidIndex) {
				DenseEntry& existing = denseArray[sparseArray[index]];
				assert(existing.entity == entity && "a destroyed entity's component outlived it");
				existing.component = T{std::forward<Args>(args)...};
				return existing.component;
			}

			// Add new entry
			size_t denseIndex = denseArray.size();
			sparseArray[index] = static_cast<uint32_t>(denseIndex);
			denseArray.push_back({entity, T{std::forward<Args>(args)...}});

			return denseArray.back().component;
		}

		/// A component held by no entity, for an add the Registry refused: writes through it reach
		/// nothing.
		template <typename... Args>
		T& refuse(Args&&... args) {
			refused = T{std::forward<Args>(args)...};
			return *refused;
		}

		/// Get component for entity, returns nullptr if not found
		[[nodiscard]] T* get(EntityID entity) {
			const uint32_t denseIndex = find(entity);
			return denseIndex == kInvalidIndex ? nullptr : &denseArray[denseIndex].component;
		}

		/// Get component for entity (const version)
		[[nodiscard]] const T* get(EntityID entity) const {
			const uint32_t denseIndex = find(entity);
			return denseIndex == kInvalidIndex ? nullptr : &denseArray[denseIndex].component;
		}

		/// Remove component from entity
		void remove(EntityID entity) override {
			const uint32_t denseIndex = find(entity);
			if (denseIndex == kInvalidIndex) {
				return;
			}

			// Swap with last element for O(1) removal
			uint32_t lastDenseIndex = static_cast<uint32_t>(denseArray.size() - 1);

			if (denseIndex != lastDenseIndex) {
				// Move last element to fill the gap
				denseArray[denseIndex] = std::move(denseArray[lastDenseIndex]);
				// Update sparse array for moved element
				sparseArray[getIndex(denseArray[denseIndex].entity)] = denseIndex;
			}

			denseArray.pop_back();
			sparseArray[getIndex(entity)] = kInvalidIndex;
		}

		/// Check if entity has this component
		[[nodiscard]] bool has(EntityID entity) const override { return find(entity) != kInvalidIndex; }

		/// Get number of components stored
		[[nodiscard]] size_t size() const override { return denseArray.size(); }

		/// Get entity at dense index (for iteration)
		[[nodiscard]] EntityID getEntity(size_t denseIndex) const {
			assert(denseIndex < denseArray.size());
			return denseArray[denseIndex].entity;
		}

		/// Get component at dense index (for iteration)
		[[nodiscard]] T& getComponent(size_t denseIndex) {
			assert(denseIndex < denseArray.size());
			return denseArray[denseIndex].component;
		}

		/// Get component at dense index (const version)
		[[nodiscard]] const T& getComponent(size_t denseIndex) const {
			assert(denseIndex < denseArray.size());
			return denseArray[denseIndex].component;
		}

	  private:
		static constexpr uint32_t kInvalidIndex = UINT32_MAX;

		struct DenseEntry {
			EntityID entity;
			T		 component;
		};

		[[nodiscard]] uint32_t find(EntityID entity) const {
			const uint32_t index = getIndex(entity);
			if (index >= sparseArray.size()) {
				return kInvalidIndex;
			}
			const uint32_t denseIndex = sparseArray[index];
			// The slot is keyed by index alone; the stored id's generation tells this entity from an
			// earlier one that held the same index.
			if (denseIndex == kInvalidIndex || denseArray[denseIndex].entity != entity) {
				return kInvalidIndex;
			}
			return denseIndex;
		}

		std::vector<uint32_t>	sparseArray; // Entity index -> dense index
		std::vector<DenseEntry> denseArray;	 // Packed component storage
		std::optional<T>		refused;
	};

} // namespace ecs
