#pragma once

// InstanceData - Data structures for GPU instancing
//
// These structures define the per-instance data uploaded to the GPU
// for instanced rendering of entities.

#include "gl/GLBuffer.h"
#include "gl/GLVertexArray.h"
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
static_assert(alignof(InstanceData) == 16, "InstanceData must be 16-byte aligned for GPU upload");

/// Handle to a mesh uploaded for instanced rendering.
/// Created by BatchRenderer::uploadInstancedMesh(), released by releaseInstancedMesh().
/// Uses RAII for automatic GPU resource cleanup - resources are freed when the handle is destroyed.
/// Movable but not copyable (GPU resources have single ownership).
struct InstancedMeshHandle {
	GLVertexArray vao;			 // VAO with mesh + instance attributes configured
	GLBuffer	  meshVBO;		 // Vertex buffer for mesh data (static)
	GLBuffer	  meshIBO;		 // Index buffer for mesh triangles
	GLBuffer	  instanceVBO;	 // Instance data buffer (per-instance, divisor=1)
	uint32_t	  indexCount = 0;	 // Number of indices in mesh
	uint32_t	  vertexCount = 0;	 // Number of vertices in mesh (for stats)
	uint32_t	  maxInstances = 0;	 // Capacity of instance buffer

	// Default constructor - creates an invalid handle
	InstancedMeshHandle() = default;

	// Non-copyable (RAII wrappers are non-copyable)
	InstancedMeshHandle(const InstancedMeshHandle&) = delete;
	InstancedMeshHandle& operator=(const InstancedMeshHandle&) = delete;

	// Movable
	InstancedMeshHandle(InstancedMeshHandle&&) = default;
	InstancedMeshHandle& operator=(InstancedMeshHandle&&) = default;

	/// Check if this handle is valid (has GPU resources)
	[[nodiscard]] bool isValid() const { return vao.isValid(); }
};

/// Vertex format for instanced meshes (simpler than UberVertex)
/// Only position and color needed - the uber shader handles the rest
struct InstancedMeshVertex {
	Foundation::Vec2  position;	 // Local mesh position (will be transformed by instance data)
	Foundation::Color color;	 // Vertex color (will be multiplied by instance color tint)
};

} // namespace Renderer
