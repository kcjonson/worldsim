#pragma once

// Foundation type aliases for common math types.
// Provides convenient aliases for GLM vector and matrix types used throughout the codebase.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Foundation { // NOLINT(readability-identifier-naming)

	// Type aliases using GLM
	using Vec2 = glm::vec2;
	using Vec3 = glm::vec3;
	using Vec4 = glm::vec4;
	using Mat4 = glm::mat4;

	// Integer vectors
	using IVec2 = glm::ivec2;
	using IVec3 = glm::ivec3;
	using IVec4 = glm::ivec4;

} // namespace Foundation
