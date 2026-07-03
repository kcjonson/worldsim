#pragma once

// SelectionOutline - a selected entity's silhouette, injected into the entity
// depth-sort stream so a nearer entity correctly occludes a farther selection's
// outline. Built by SelectionSystem (world-space rings + the ground-contact
// anchorY depth key), consumed by InstancedEntityRenderer::emitSorted.

#include <math/Types.h>

#include <glm/vec4.hpp>

#include <vector>

namespace engine::world {

	struct SelectionOutline {
		std::vector<std::vector<Foundation::Vec2>> worldRings; // closed loops, world meters
		float	  anchorY = 0.0F;					// inject just behind entities with larger anchorY
		glm::vec4 rgba{1.0F, 0.85F, 0.0F, 0.9F};
		float	  widthPx = 4.0F;
		bool	  valid = false;
	};

} // namespace engine::world
