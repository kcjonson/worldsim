#pragma once

#include "ComponentPool.h"
#include "EntityID.h"
#include "Registry.h"

#include <tuple>
#include <type_traits>

namespace ecs {

/// View for iterating entities with specific components.
/// Iterates over the smallest component pool and filters by other components.
template <typename... Components>
class View {
public:
    explicit View(Registry& registry) : registry(registry) {}

    /// Iterator for View
    class Iterator {
    public:
        Iterator(Registry& registry, size_t index, size_t size)
            : registry(registry), currentIndex(index), poolSize(size) {
            // Skip to first valid entity
            skipInvalid();
        }

        auto operator*() {
            auto* smallestPool = registry.getPool<FirstComponent>();
            EntityID entity = smallestPool->getEntity(currentIndex);
            return std::tuple<EntityID, Components&...>{
                entity, *registry.getComponent<Components>(entity)...};
        }

        Iterator& operator++() {
            ++currentIndex;
            skipInvalid();
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return currentIndex != other.currentIndex;
        }

    private:
        using FirstComponent = std::tuple_element_t<0, std::tuple<Components...>>;

        void skipInvalid() {
            auto* smallestPool = registry.getPool<FirstComponent>();
            if (!smallestPool) {
                currentIndex = poolSize;
                return;
            }

            while (currentIndex < poolSize) {
                EntityID entity = smallestPool->getEntity(currentIndex);
                if (hasAllComponents(entity)) {
                    break;
                }
                ++currentIndex;
            }
        }

        bool hasAllComponents(EntityID entity) {
            return (registry.hasComponent<Components>(entity) && ...);
        }

        Registry& registry;
        size_t currentIndex;
        size_t poolSize;
    };

    [[nodiscard]] Iterator begin() {
        size_t size = getSmallestPoolSize();
        return Iterator(registry, 0, size);
    }

    [[nodiscard]] Iterator end() {
        size_t size = getSmallestPoolSize();
        return Iterator(registry, size, size);
    }

private:
    using FirstComponent = std::tuple_element_t<0, std::tuple<Components...>>;

    [[nodiscard]] size_t getSmallestPoolSize() const {
        // For simplicity, use first component's pool size
        // A more optimal implementation would find the actual smallest pool
        auto* pool = registry.template getPool<FirstComponent>();
        return pool ? pool->size() : 0;
    }

    Registry& registry;
};

}  // namespace ecs
