#pragma once

#include <cstdint>

namespace ecs {

/// The kind of structural element this entity represents.
/// Only Foundation is used in Epic C; Wall/Opening/Room are forward-looking.
enum class StructureKind {
    Foundation,
    Wall,
    Opening,
    Room,
};

/// Links an ECS entity to its corresponding node in the ConstructionWorld
/// topology graph. graphId 0 means the entity is not yet linked to any
/// topology node (freshly created, or topology creation deferred).
struct Structure {
    StructureKind kind = StructureKind::Foundation;
    uint64_t graphId = 0;  // 0 = unlinked
};

}  // namespace ecs
