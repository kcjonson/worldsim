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
    virtual void remove(EntityID entity) = 0;
    [[nodiscard]] virtual bool has(EntityID entity) const = 0;
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
        if (index >= m_sparse.size()) {
            m_sparse.resize(index + 1, kInvalidIndex);
        }

        // Check if entity already has component
        if (m_sparse[index] != kInvalidIndex) {
            // Replace existing component
            m_dense[m_sparse[index]].component = T{std::forward<Args>(args)...};
            return m_dense[m_sparse[index]].component;
        }

        // Add new entry
        size_t denseIndex = m_dense.size();
        m_sparse[index] = static_cast<uint32_t>(denseIndex);
        m_dense.push_back({entity, T{std::forward<Args>(args)...}});

        return m_dense.back().component;
    }

    /// Get component for entity, returns nullptr if not found
    [[nodiscard]] T* get(EntityID entity) {
        uint32_t index = getIndex(entity);
        if (index >= m_sparse.size() || m_sparse[index] == kInvalidIndex) {
            return nullptr;
        }
        return &m_dense[m_sparse[index]].component;
    }

    /// Get component for entity (const version)
    [[nodiscard]] const T* get(EntityID entity) const {
        uint32_t index = getIndex(entity);
        if (index >= m_sparse.size() || m_sparse[index] == kInvalidIndex) {
            return nullptr;
        }
        return &m_dense[m_sparse[index]].component;
    }

    /// Remove component from entity
    void remove(EntityID entity) override {
        uint32_t index = getIndex(entity);
        if (index >= m_sparse.size() || m_sparse[index] == kInvalidIndex) {
            return;
        }

        // Swap with last element for O(1) removal
        uint32_t denseIndex = m_sparse[index];
        uint32_t lastDenseIndex = static_cast<uint32_t>(m_dense.size() - 1);

        if (denseIndex != lastDenseIndex) {
            // Move last element to fill the gap
            m_dense[denseIndex] = std::move(m_dense[lastDenseIndex]);
            // Update sparse array for moved element
            m_sparse[getIndex(m_dense[denseIndex].entity)] = denseIndex;
        }

        m_dense.pop_back();
        m_sparse[index] = kInvalidIndex;
    }

    /// Check if entity has this component
    [[nodiscard]] bool has(EntityID entity) const override {
        uint32_t index = getIndex(entity);
        return index < m_sparse.size() && m_sparse[index] != kInvalidIndex;
    }

    /// Get number of components stored
    [[nodiscard]] size_t size() const override {
        return m_dense.size();
    }

    /// Get entity at dense index (for iteration)
    [[nodiscard]] EntityID getEntity(size_t denseIndex) const {
        assert(denseIndex < m_dense.size());
        return m_dense[denseIndex].entity;
    }

    /// Get component at dense index (for iteration)
    [[nodiscard]] T& getComponent(size_t denseIndex) {
        assert(denseIndex < m_dense.size());
        return m_dense[denseIndex].component;
    }

    /// Get component at dense index (const version)
    [[nodiscard]] const T& getComponent(size_t denseIndex) const {
        assert(denseIndex < m_dense.size());
        return m_dense[denseIndex].component;
    }

private:
    static constexpr uint32_t kInvalidIndex = UINT32_MAX;

    struct DenseEntry {
        EntityID entity;
        T component;
    };

    std::vector<uint32_t> m_sparse;   // Entity index -> dense index
    std::vector<DenseEntry> m_dense;  // Packed component storage
};

}  // namespace ecs
