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
    explicit View(Registry& registry) : m_registry(registry) {}

    /// Iterator for View
    class Iterator {
    public:
        Iterator(Registry& registry, size_t index, size_t size)
            : m_registry(registry), m_index(index), m_size(size) {
            // Skip to first valid entity
            skipInvalid();
        }

        auto operator*() {
            auto* smallestPool = m_registry.getPool<FirstComponent>();
            EntityID entity = smallestPool->getEntity(m_index);
            return std::tuple<EntityID, Components&...>{
                entity, *m_registry.getComponent<Components>(entity)...};
        }

        Iterator& operator++() {
            ++m_index;
            skipInvalid();
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return m_index != other.m_index;
        }

    private:
        using FirstComponent = std::tuple_element_t<0, std::tuple<Components...>>;

        void skipInvalid() {
            auto* smallestPool = m_registry.getPool<FirstComponent>();
            if (!smallestPool) {
                m_index = m_size;
                return;
            }

            while (m_index < m_size) {
                EntityID entity = smallestPool->getEntity(m_index);
                if (hasAllComponents(entity)) {
                    break;
                }
                ++m_index;
            }
        }

        bool hasAllComponents(EntityID entity) {
            return (m_registry.hasComponent<Components>(entity) && ...);
        }

        Registry& m_registry;
        size_t m_index;
        size_t m_size;
    };

    [[nodiscard]] Iterator begin() {
        size_t size = getSmallestPoolSize();
        return Iterator(m_registry, 0, size);
    }

    [[nodiscard]] Iterator end() {
        size_t size = getSmallestPoolSize();
        return Iterator(m_registry, size, size);
    }

private:
    using FirstComponent = std::tuple_element_t<0, std::tuple<Components...>>;

    [[nodiscard]] size_t getSmallestPoolSize() const {
        // For simplicity, use first component's pool size
        // A more optimal implementation would find the actual smallest pool
        auto* pool = m_registry.template getPool<FirstComponent>();
        return pool ? pool->size() : 0;
    }

    Registry& m_registry;
};

}  // namespace ecs
