#pragma once

// Shared infrastructure for the instanced/baked render paths: cached uniform
// locations (one glGetUniformLocation pass) and the per-frame metrics the
// orchestrator aggregates across sub-renderers.

#include <GL/glew.h>
#include <cstdint>

namespace engine::world {

/// Per-frame draw metrics. The orchestrator owns the canonical counters, resets
/// them once at the top of the instanced path, and passes them by reference so
/// each sub-renderer ADDS its contribution (raw GL draws bypass the
/// BatchRenderer stats, so the metrics system reads them from here).
struct RenderStats {
	uint32_t entities = 0;
	uint32_t drawCalls = 0;
	uint32_t triangles = 0;
};

/// Cached uniform locations for instanced rendering (avoid glGetUniformLocation per frame).
struct InstancingUniforms {
	GLint projection = -1;
	GLint transform = -1;
	GLint instanced = -1;
	GLint cameraPosition = -1;
	GLint cameraZoom = -1;
	GLint pixelsPerMeter = -1;
	GLint viewportSize = -1;
	GLint bakedAlpha = -1;
	// Groundcover deform uniforms (instancing.glsl); set when drawing groundcover.
	GLint groundcoverMode = -1;
	GLint groundcoverOpenness = -1;
	GLint groundcoverReach = -1;
	GLint cursorWorld = -1;
	GLint cursorRadius = -1;
	GLint cursorStrength = -1;
	bool initialized = false;

	/// Initialize cached uniform locations from shader program.
	void init(GLuint shaderProgram) {
		if (initialized) {
			return;
		}
		projection = glGetUniformLocation(shaderProgram, "u_projection");
		transform = glGetUniformLocation(shaderProgram, "u_transform");
		instanced = glGetUniformLocation(shaderProgram, "u_instanced");
		cameraPosition = glGetUniformLocation(shaderProgram, "u_cameraPosition");
		cameraZoom = glGetUniformLocation(shaderProgram, "u_cameraZoom");
		pixelsPerMeter = glGetUniformLocation(shaderProgram, "u_pixelsPerMeter");
		viewportSize = glGetUniformLocation(shaderProgram, "u_viewportSize");
		bakedAlpha = glGetUniformLocation(shaderProgram, "u_bakedAlpha");
		groundcoverMode = glGetUniformLocation(shaderProgram, "u_groundcoverMode");
		groundcoverOpenness = glGetUniformLocation(shaderProgram, "u_groundcoverOpenness");
		groundcoverReach = glGetUniformLocation(shaderProgram, "u_groundcoverReach");
		cursorWorld = glGetUniformLocation(shaderProgram, "u_cursorWorld");
		cursorRadius = glGetUniformLocation(shaderProgram, "u_cursorRadius");
		cursorStrength = glGetUniformLocation(shaderProgram, "u_cursorStrength");
		initialized = true;
	}
};

}  // namespace engine::world
