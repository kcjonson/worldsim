#pragma once

#include <assets/TreeTraits.h>

#include <glm/vec4.hpp>
#include <string>

namespace ecs {

/// Visual appearance of an entity for rendering
struct Appearance {
    std::string defName;  // Asset definition name (e.g., "Colonist")
    float scale = 1.0f;
    glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
    assets::TreeTraits traits{};  // Age class and mesh variant (computeTreeTraits) for the renderer
};

}  // namespace ecs
