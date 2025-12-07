#pragma once

#include <glm/vec4.hpp>
#include <string>

namespace ecs {

/// Visual appearance of an entity for rendering
struct Appearance {
    std::string defName;  // Asset definition name (e.g., "Colonist")
    float scale = 1.0f;
    glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
};

}  // namespace ecs
