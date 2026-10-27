#pragma once

// The groundcover hook's uniform block (shaders/hooks/groundcover.vert.glsl), member for
// member, as std140 lays it out. GroundcoverRenderer and ui-sandbox's grass scene fill it
// before their instanced draws.

#include "BaseShader.h"

#include <math/Types.h>
#include <primitives/Primitives.h>

#include <cstddef>
#include <cstdint>
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
	static_assert(offsetof(GroundcoverUniforms, cursorWorld) == 0);
	static_assert(offsetof(GroundcoverUniforms, enabled) == 8);
	static_assert(offsetof(GroundcoverUniforms, openness) == 12);
	static_assert(offsetof(GroundcoverUniforms, reach) == 16);
	static_assert(offsetof(GroundcoverUniforms, cursorRadius) == 20);
	static_assert(offsetof(GroundcoverUniforms, cursorStrength) == 24);
	static_assert(sizeof(GroundcoverUniforms) == base_shader::kGroundcover.size, "the block the build generated from the hook");

	// The deform for the instanced draws issued from here on; {} switches it off.
	inline void setGroundcoverUniforms(const GroundcoverUniforms& uniforms) {
		Renderer::Primitives::setHookUniforms(base_shader::kGroundcover, std::as_bytes(std::span(&uniforms, 1)));
	}

} // namespace engine::world
