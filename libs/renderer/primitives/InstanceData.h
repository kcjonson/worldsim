#pragma once

// InstanceData - Data structures for GPU instancing
//
// These structures define the per-instance data uploaded to the GPU
// for instanced rendering of entities.

#include "graphics/Color.h"
#include "math/Types.h"
#include <GL/glew.h>
#include <cstdint>

namespace Renderer {

/// Per-instance data for GPU instancing (32 bytes, 16-byte aligned)
/// This data is uploaded once per instance and used by the vertex shader
/// to transform mesh vertices to screen space.
struct alignas(16) InstanceData {
	Foundation::Vec2 worldPosition;	 // World-space position (8 bytes)
	float			 rotation;		 // Rotation in radians (4 bytes)
	float			 scale;			 // Uniform scale multiplier (4 bytes)
	Foundation::Vec4 colorTint;		 // RGBA color multiplier (16 bytes)

	/// Default constructor - identity instance at origin
	InstanceData()
		: worldPosition(0.0F, 0.0F)
		, rotation(0.0F)
		, scale(1.0F)
		, colorTint(1.0F, 1.0F, 1.0F, 1.0F) {}

	/// Full constructor
	InstanceData(Foundation::Vec2 pos, float rot, float scl, Foundation::Vec4 tint)
		: worldPosition(pos)
		, rotation(rot)
		, scale(scl)
		, colorTint(tint) {}

	/// Convenience constructor with Color
	InstanceData(Foundation::Vec2 pos, float rot, float scl, const Foundation::Color& tint)
		: worldPosition(pos)
		, rotation(rot)
		, scale(scl)
		, colorTint(tint.r, tint.g, tint.b, tint.a) {}
};

static_assert(sizeof(InstanceData) == 32, "InstanceData must be 32 bytes for GPU alignment");

/// Handle to a mesh uploaded for instanced rendering.
/// Created by BatchRenderer::uploadInstancedMesh(), released by releaseInstancedMesh().
struct InstancedMeshHandle {
	GLuint	 vao = 0;			 // VAO with mesh + instance attributes configured
	GLuint	 meshVBO = 0;		 // Vertex buffer for mesh data (static)
	GLuint	 meshIBO = 0;		 // Index buffer for mesh triangles
	GLuint	 instanceVBO = 0;	 // Instance data buffer (per-instance, divisor=1)
	uint32_t indexCount = 0;	 // Number of indices in mesh
	uint32_t maxInstances = 0;	 // Capacity of instance buffer

	/// Check if this handle is valid (has GPU resources)
	[[nodiscard]] bool isValid() const { return vao != 0; }
};

/// Vertex format for instanced meshes (simpler than UberVertex)
/// Only position and color needed - the uber shader handles the rest
struct InstancedMeshVertex {
	Foundation::Vec2  position;	 // Local mesh position (will be transformed by instance data)
	Foundation::Color color;	 // Vertex color (will be multiplied by instance color tint)
};

} // namespace Renderer
