#pragma once

// Foundation type aliases for common math types.
// Provides convenient aliases for GLM vector and matrix types used throughout the codebase.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>

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

	// Copy a Vec2 list into a glm::vec2 list for APIs typed on glm directly (Vec2 IS glm::vec2,
	// so this is a plain copy; the named helper keeps the intent clear at call sites that bridge
	// Foundation geometry into glm-typed nav/geometry queries).
	[[nodiscard]] inline std::vector<glm::vec2> toGlmVec2(const std::vector<Vec2>& pts) {
		return {pts.begin(), pts.end()};
	}

} // namespace Foundation
