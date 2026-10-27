#pragma once

// The groundcover hook's uniform block (hooks/groundcover.vert.glsl), member for member,
// checked against the offsets and sizes the build reflected from the shader, so a block
// reordered or grown in the GLSL stops this from compiling. GroundcoverRenderer and
// ui-sandbox's grass scene fill it before their instanced draws.

#include "BaseShader.h"

#include <math/Types.h>
#include <primitives/Primitives.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

namespace engine::world {

	struct GroundcoverUniforms {
		Foundation::Vec2 cursorWorld{0.0F, 0.0F}; // interaction centre, world space
		int32_t			 enabled = 0;			  // 1 applies the deform
		float			 openness = 1.0F;		  // resting clump scale 0..1 (1 = full size)
		float			 reach = 0.0F;			  // max local vertex distance from a clump's base (meters)
		float			 cursorRadius = 0.0F;	  // interaction radius, world space
		float			 cursorStrength = 0.0F;	  // push distance at a clump's outer tips, world space
		float			 padding = 0.0F;		  // std140 pads the block to 16 bytes
	};
	static_assert(sizeof(GroundcoverUniforms) == base_shader::kGroundcover.size);
	static_assert(std::size(base_shader::kGroundcoverMembers) == 6, "a member added to the GLSL block needs one here");
	static_assert(offsetof(GroundcoverUniforms, cursorWorld) == base_shader::kGroundcoverCursorWorld.offset);
	static_assert(sizeof(GroundcoverUniforms::cursorWorld) == base_shader::kGroundcoverCursorWorld.size);
	static_assert(offsetof(GroundcoverUniforms, enabled) == base_shader::kGroundcoverEnabled.offset);
	static_assert(sizeof(GroundcoverUniforms::enabled) == base_shader::kGroundcoverEnabled.size);
	static_assert(offsetof(GroundcoverUniforms, openness) == base_shader::kGroundcoverOpenness.offset);
	static_assert(sizeof(GroundcoverUniforms::openness) == base_shader::kGroundcoverOpenness.size);
	static_assert(offsetof(GroundcoverUniforms, reach) == base_shader::kGroundcoverReach.offset);
	static_assert(sizeof(GroundcoverUniforms::reach) == base_shader::kGroundcoverReach.size);
	static_assert(offsetof(GroundcoverUniforms, cursorRadius) == base_shader::kGroundcoverCursorRadius.offset);
	static_assert(sizeof(GroundcoverUniforms::cursorRadius) == base_shader::kGroundcoverCursorRadius.size);
	static_assert(offsetof(GroundcoverUniforms, cursorStrength) == base_shader::kGroundcoverCursorStrength.offset);
	static_assert(sizeof(GroundcoverUniforms::cursorStrength) == base_shader::kGroundcoverCursorStrength.size);

	// The deform for the instanced draws issued from here on; {} switches it off.
	inline void setGroundcoverUniforms(const GroundcoverUniforms& uniforms) {
		Renderer::Primitives::setHookUniforms(base_shader::kGroundcover, std::as_bytes(std::span(&uniforms, 1)));
	}

} // namespace engine::world
