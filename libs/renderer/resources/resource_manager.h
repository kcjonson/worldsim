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
		explicit ResourceManager(size_t capacity = 1024) {
			m_resources.reserve(capacity);
			m_generations.reserve(capacity);
			m_freeIndices.reserve(capacity);
		}

		// Allocate new resource slot
		ResourceHandle Allocate() {
			uint16_t index;

			if (!m_freeIndices.empty()) {
				// Reuse freed slot
				index = m_freeIndices.back();
				m_freeIndices.pop_back();
			} else {
				// Allocate new slot - check BEFORE cast to prevent wraparound
				assert(m_resources.size() < 65536 && "ResourceManager out of slots");
				index = static_cast<uint16_t>(m_resources.size());
				m_resources.emplace_back();
				m_generations.push_back(0);
			}

			return ResourceHandle::Make(index, m_generations[index]);
		}

		// Free resource slot
		void Free(ResourceHandle handle) {
			if (!handle.IsValid()) {
				return;
			}

			uint16_t index = handle.GetIndex();
			assert(index < m_resources.size() && "Invalid handle index");

			// Check generation matches (prevent double-free of stale handles)
			if (handle.GetGeneration() != m_generations[index]) {
				return; // Stale handle, already freed
			}

			// Increment generation (invalidates old handles)
			m_generations[index]++;

			// Add to free list
			m_freeIndices.push_back(index);
		}

		// Get resource (validates handle)
		T* Get(ResourceHandle handle) {
			if (!handle.IsValid()) {
				return nullptr;
			}

			uint16_t index = handle.GetIndex();
			if (index >= m_resources.size()) {
				return nullptr;
			}

			// Check generation
			if (handle.GetGeneration() != m_generations[index]) {
				return nullptr; // Stale handle
			}

			return &m_resources[index];
		}

		// Get resource (const)
		const T* Get(ResourceHandle handle) const {
			if (!handle.IsValid()) {
				return nullptr;
			}

			uint16_t index = handle.GetIndex();
			if (index >= m_resources.size()) {
				return nullptr;
			}

			// Check generation
			if (handle.GetGeneration() != m_generations[index]) {
				return nullptr; // Stale handle
			}

			return &m_resources[index];
		}

		// Get resource count (includes freed slots)
		size_t GetCount() const { return m_resources.size(); }

		// Get active resource count (excludes freed slots)
		size_t GetActiveCount() const { return m_resources.size() - m_freeIndices.size(); }

		// Clear all resources
		void Clear() {
			m_resources.clear();
			m_generations.clear();
			m_freeIndices.clear();
		}

	  private:
		std::vector<T>		  m_resources;	 // Resource storage
		std::vector<uint16_t> m_generations; // Generation counter per slot
		std::vector<uint16_t> m_freeIndices; // Free list for recycling
	};

} // namespace renderer
