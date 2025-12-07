#pragma once

#include "EntityID.h"

#include <cassert>
#include <cstddef>
#include <memory>
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
	template <typename T>
	class ComponentPool : public IComponentPool {
	  public:
		/// Add component to entity, returning reference to the new component
		template <typename... Args>
		T& add(EntityID entity, Args&&... args) {
			uint32_t index = getIndex(entity);

			// Ensure sparse array is large enough
			if (index >= sparseArray.size()) {
				sparseArray.resize(index + 1, kInvalidIndex);
			}

			// Check if entity already has component
			if (sparseArray[index] != kInvalidIndex) {
				// Replace existing component
				denseArray[sparseArray[index]].component = T{std::forward<Args>(args)...};
				return denseArray[sparseArray[index]].component;
			}

			// Add new entry
			size_t denseIndex = denseArray.size();
			sparseArray[index] = static_cast<uint32_t>(denseIndex);
			denseArray.push_back({entity, T{std::forward<Args>(args)...}});

			return denseArray.back().component;
		}

		/// Get component for entity, returns nullptr if not found
		[[nodiscard]] T* get(EntityID entity) {
			uint32_t index = getIndex(entity);
			if (index >= sparseArray.size() || sparseArray[index] == kInvalidIndex) {
				return nullptr;
			}
			return &denseArray[sparseArray[index]].component;
		}

		/// Get component for entity (const version)
		[[nodiscard]] const T* get(EntityID entity) const {
			uint32_t index = getIndex(entity);
			if (index >= sparseArray.size() || sparseArray[index] == kInvalidIndex) {
				return nullptr;
			}
			return &denseArray[sparseArray[index]].component;
		}

		/// Remove component from entity
		void remove(EntityID entity) override {
			uint32_t index = getIndex(entity);
			if (index >= sparseArray.size() || sparseArray[index] == kInvalidIndex) {
				return;
			}

			// Swap with last element for O(1) removal
			uint32_t denseIndex = sparseArray[index];
			uint32_t lastDenseIndex = static_cast<uint32_t>(denseArray.size() - 1);

			if (denseIndex != lastDenseIndex) {
				// Move last element to fill the gap
				denseArray[denseIndex] = std::move(denseArray[lastDenseIndex]);
				// Update sparse array for moved element
				sparseArray[getIndex(denseArray[denseIndex].entity)] = denseIndex;
			}

			denseArray.pop_back();
			sparseArray[index] = kInvalidIndex;
		}

		/// Check if entity has this component
		[[nodiscard]] bool has(EntityID entity) const override {
			uint32_t index = getIndex(entity);
			return index < sparseArray.size() && sparseArray[index] != kInvalidIndex;
		}

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

		std::vector<uint32_t>	sparseArray; // Entity index -> dense index
		std::vector<DenseEntry> denseArray;	 // Packed component storage
	};

} // namespace ecs
