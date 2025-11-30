#pragma once

// Resource Manager Template
//
// Generic manager for handle-based resources with generation validation.
// Provides safe allocation, deallocation, and retrieval of resources.
//
// Features:
// - Free list for recycled indices (O(1) allocation)
// - Generation counter prevents stale handle access
// - Type-safe via templates
// - Capacity: 65,536 resources max (16-bit index)
//
// Not thread-safe - add mutex if accessing from multiple threads.

#include "resource_handle.h"
#include <cassert>
#include <vector>

namespace renderer {

	template <typename T>
	class ResourceManager {
	  public:
		explicit ResourceManager(size_t capacity = 1024) { // NOLINT(cppcoreguidelines-pro-type-member-init)
			resources.reserve(capacity);
			generations.reserve(capacity);
			freeIndices.reserve(capacity);
		}

		// Allocate new resource slot
		ResourceHandle Allocate() {
			uint16_t index;

			if (!freeIndices.empty()) {
				// Reuse freed slot
				index = freeIndices.back();
				freeIndices.pop_back();
			} else {
				// Allocate new slot - check BEFORE cast to prevent wraparound
				assert(resources.size() < 65536 && "ResourceManager out of slots");
				index = static_cast<uint16_t>(resources.size());
				resources.emplace_back();
				generations.push_back(0);
			}

			return ResourceHandle::make(index, generations[index]);
		}

		// Free resource slot
		void free(ResourceHandle handle) {
			if (!handle.isValid()) {
				return;
			}

			uint16_t index = handle.getIndex();
			assert(index < resources.size() && "Invalid handle index");

			// Check generation matches (prevent double-free of stale handles)
			if (handle.getGeneration() != generations[index]) {
				return; // Stale handle, already freed
			}

			// Increment generation (invalidates old handles)
			generations[index]++;

			// Add to free list
			freeIndices.push_back(index);
		}

		// Get resource (validates handle)
		T* Get(ResourceHandle handle) {
			if (!handle.isValid()) {
				return nullptr;
			}

			uint16_t index = handle.getIndex();
			if (index >= resources.size()) {
				return nullptr;
			}

			// Check generation
			if (handle.getGeneration() != generations[index]) {
				return nullptr; // Stale handle
			}

			return &resources[index];
		}

		// Get resource (const)
		const T* Get(ResourceHandle handle) const {
			if (!handle.isValid()) {
				return nullptr;
			}

			uint16_t index = handle.getIndex();
			if (index >= resources.size()) {
				return nullptr;
			}

			// Check generation
			if (handle.getGeneration() != generations[index]) {
				return nullptr; // Stale handle
			}

			return &resources[index];
		}

		// Get resource count (includes freed slots)
		size_t getCount() const { return resources.size(); }

		// Get active resource count (excludes freed slots)
		size_t getActiveCount() const { return resources.size() - freeIndices.size(); }

		// Clear all resources
		void clear() {
			resources.clear();
			generations.clear();
			freeIndices.clear();
		}

	  private:
		std::vector<T>		  resources;	 // Resource storage
		std::vector<uint16_t> generations; // Generation counter per slot
		std::vector<uint16_t> freeIndices; // Free list for recycling
	};

} // namespace renderer
