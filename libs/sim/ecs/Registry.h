#pragma once

#include "ComponentPool.h"
#include "EntityID.h"

#include <memory>
#include <queue>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace ecs {

/// Manages entity lifecycle and component storage.
/// Provides O(1) entity creation/destruction and component operations.
class Registry {
public:
    Registry() = default;
    ~Registry() = default;

    // Non-copyable, movable
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = default;
    Registry& operator=(Registry&&) = default;

    /// Create a new entity, reusing IDs when possible
    [[nodiscard]] EntityID createEntity() {
        EntityID entity = kInvalidEntity;

        if (!freeList.empty()) {
            // Reuse a recycled index with incremented generation
            uint32_t index = freeList.front();
            freeList.pop();
            uint32_t generation = generations[index];
            entity = makeEntityID(index, generation);
        } else {
            // Allocate new index
            uint32_t index = static_cast<uint32_t>(generations.size());
            generations.push_back(1);  // Start at generation 1
            entity = makeEntityID(index, 1);
        }

        livingCount++;
        return entity;
    }

    /// Destroy an entity, marking it for ID recycling
    void destroyEntity(EntityID entity) {
        if (!isAlive(entity)) {
            return;
        }

        uint32_t index = getIndex(entity);

        // Remove all components
        for (auto& [typeIndex, pool] : pools) {
            pool->remove(entity);
        }

        // Increment generation to invalidate existing handles
        generations[index]++;
        freeList.push(index);
        livingCount--;
    }

    /// Check if an entity is still alive
    [[nodiscard]] bool isAlive(EntityID entity) const {
        if (entity == kInvalidEntity) {
            return false;
        }
        uint32_t index = getIndex(entity);
        uint32_t generation = getGeneration(entity);
        return index < generations.size() && generations[index] == generation;
    }

    /// Add a component to an entity
    template <typename T, typename... Args>
    T& addComponent(EntityID entity, Args&&... args) {
        return getOrCreatePool<T>().add(entity, std::forward<Args>(args)...);
    }

    /// Get a component from an entity (returns nullptr if not found)
    template <typename T>
    [[nodiscard]] T* getComponent(EntityID entity) {
        auto* pool = getPool<T>();
        return pool ? pool->get(entity) : nullptr;
    }

    /// Get a component from an entity (const version)
    template <typename T>
    [[nodiscard]] const T* getComponent(EntityID entity) const {
        auto* pool = getPool<T>();
        return pool ? pool->get(entity) : nullptr;
    }

    /// Check if entity has a component
    template <typename T>
    [[nodiscard]] bool hasComponent(EntityID entity) const {
        auto* pool = getPool<T>();
        return pool && pool->has(entity);
    }

    /// Remove a component from an entity
    template <typename T>
    void removeComponent(EntityID entity) {
        if (auto* pool = getPool<T>()) {
            pool->remove(entity);
        }
    }

    /// Get the component pool for a type (returns nullptr if none exists)
    template <typename T>
    [[nodiscard]] ComponentPool<T>* getPool() {
        auto it = pools.find(std::type_index(typeid(T)));
        if (it == pools.end()) {
            return nullptr;
        }
        return static_cast<ComponentPool<T>*>(it->second.get());
    }

    /// Get the component pool for a type (const version)
    template <typename T>
    [[nodiscard]] const ComponentPool<T>* getPool() const {
        auto it = pools.find(std::type_index(typeid(T)));
        if (it == pools.end()) {
            return nullptr;
        }
        return static_cast<const ComponentPool<T>*>(it->second.get());
    }

    /// Get the number of living entities
    [[nodiscard]] size_t getLivingCount() const {
        return livingCount;
    }

private:
    template <typename T>
    ComponentPool<T>& getOrCreatePool() {
        auto typeIndex = std::type_index(typeid(T));
        auto it = pools.find(typeIndex);
        if (it == pools.end()) {
            auto pool = std::make_unique<ComponentPool<T>>();
            auto* rawPtr = pool.get();
            pools[typeIndex] = std::move(pool);
            return *rawPtr;
        }
        return *static_cast<ComponentPool<T>*>(it->second.get());
    }

    std::vector<uint32_t> generations;  // Generation counter per entity index
    std::queue<uint32_t> freeList;      // Recycled entity indices
    size_t livingCount = 0;

    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools;
};

}  // namespace ecs
