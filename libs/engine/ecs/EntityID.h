#pragma once

#include <cstdint>

namespace ecs {

/// Entity identifier with generation counter for safe handle reuse.
/// Lower 32 bits: index, Upper 32 bits: generation
/// Note: EntityID is a typedef for uint64_t, so std::hash works out of the box.
using EntityID = uint64_t;

/// Invalid entity constant
constexpr EntityID kInvalidEntity = 0;

/// Extract the index portion of an EntityID
[[nodiscard]] constexpr uint32_t getIndex(EntityID id) {
    return static_cast<uint32_t>(id & 0xFFFFFFFF);
}

/// Extract the generation portion of an EntityID
[[nodiscard]] constexpr uint32_t getGeneration(EntityID id) {
    return static_cast<uint32_t>(id >> 32);
}

/// Create an EntityID from index and generation
[[nodiscard]] constexpr EntityID makeEntityID(uint32_t index, uint32_t generation) {
    return static_cast<EntityID>(index) | (static_cast<EntityID>(generation) << 32);
}

}  // namespace ecs
