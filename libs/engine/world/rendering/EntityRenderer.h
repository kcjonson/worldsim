#pragma once

// EntityRenderer - Renders placed entities on top of chunk tiles.
// Batches all visible entities into a single draw call per frame.
// Uses view frustum culling for efficiency.
//
// Supports two rendering paths:
// 1. GPU Instancing (default): One draw call per mesh type, transforms on GPU
// 2. CPU Batching (fallback): All entities in one draw call, transforms on CPU

#include "assets/placement/PlacementExecutor.h"
#include "primitives/InstanceData.h"
#include "vector/Tessellator.h"
#include "world/camera/WorldCamera.h"
#include "world/chunk/Chunk.h"

#include <unordered_map>
#include <unordered_set>

namespace engine::world {

/// Renders entities placed by the PlacementExecutor.
/// Groups entities by asset type and batches them for efficient rendering.
class EntityRenderer {
  public:
	/// Create an entity renderer
	/// @param pixelsPerMeter Scale factor for world-to-screen conversion
	explicit EntityRenderer(float pixelsPerMeter = 16.0F);

	/// Destructor - releases GPU resources
	~EntityRenderer();

	// Non-copyable, non-movable (owns GPU resources that require explicit release)
	EntityRenderer(const EntityRenderer&) = delete;
	EntityRenderer& operator=(const EntityRenderer&) = delete;
	EntityRenderer(EntityRenderer&&) = delete;
	EntityRenderer& operator=(EntityRenderer&&) = delete;

	/// Render entities from processed chunks
	/// @param executor PlacementExecutor containing entity data
	/// @param processedChunks Set of chunk coordinates that have been processed
	/// @param camera Camera for coordinate transforms
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	void render(const assets::PlacementExecutor& executor,
				const std::unordered_set<ChunkCoordinate>& processedChunks,
				const WorldCamera& camera,
				int viewportWidth,
				int viewportHeight);

	/// Render entities from processed chunks plus additional dynamic entities
	/// @param executor PlacementExecutor containing entity data
	/// @param processedChunks Set of chunk coordinates that have been processed
	/// @param dynamicEntities Additional entities to render (e.g., from ECS)
	/// @param camera Camera for coordinate transforms
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	void render(const assets::PlacementExecutor& executor,
				const std::unordered_set<ChunkCoordinate>& processedChunks,
				const std::vector<assets::PlacedEntity>& dynamicEntities,
				const WorldCamera& camera,
				int viewportWidth,
				int viewportHeight);

	/// Set pixels per meter (zoom level)
	void setPixelsPerMeter(float pixelsPerMeter) { m_pixelsPerMeter = pixelsPerMeter; }
	[[nodiscard]] float pixelsPerMeter() const { return m_pixelsPerMeter; }

	/// Get number of entities rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastEntityCount() const { return m_lastEntityCount; }

	/// Enable/disable GPU instancing (for A/B testing and fallback)
	void setInstancingEnabled(bool enabled) { m_useInstancing = enabled; }
	[[nodiscard]] bool isInstancingEnabled() const { return m_useInstancing; }

  private:
	float m_pixelsPerMeter = 16.0F;
	uint32_t m_lastEntityCount = 0;

	// --- Instancing Mode ---
	bool m_useInstancing = true;

	// Cache for GPU mesh handles (keyed by defName)
	std::unordered_map<std::string, Renderer::InstancedMeshHandle> m_meshHandles;

	// Per-frame instance batches (grouped by mesh type, reused each frame)
	std::unordered_map<std::string, std::vector<Renderer::InstanceData>> m_instanceBatches;

	/// Get or create GPU mesh handle for a template
	Renderer::InstancedMeshHandle& getOrCreateMeshHandle(
		const std::string& defName,
		const renderer::TessellatedMesh* mesh
	);

	/// Render using GPU instancing path
	void renderInstanced(
		const assets::PlacementExecutor& executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>* dynamicEntities,
		const WorldCamera& camera,
		int viewportWidth,
		int viewportHeight
	);

	// --- CPU Batching Mode (Fallback) ---

	// Cache for template meshes (keyed by defName)
	std::unordered_map<std::string, const renderer::TessellatedMesh*> m_templateCache;

	// Per-frame geometry buffers (reused each frame)
	std::vector<Foundation::Vec2> m_vertices;
	std::vector<Foundation::Color> m_colors;
	std::vector<uint16_t> m_indices;

	/// Get or cache a template mesh
	const renderer::TessellatedMesh* getTemplate(const std::string& defName);

	/// Render using CPU batching path (original implementation)
	void renderBatched(
		const assets::PlacementExecutor& executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>* dynamicEntities,
		const WorldCamera& camera,
		int viewportWidth,
		int viewportHeight
	);
};

}  // namespace engine::world
