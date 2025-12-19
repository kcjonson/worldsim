#pragma once

// EntityRenderer - Renders placed entities on top of chunk tiles.
// Batches all visible entities into a single draw call per frame.
// Uses view frustum culling for efficiency.
//
// Supports two rendering paths:
// 1. GPU Instancing (default): One draw call per mesh type, transforms on GPU
// 2. CPU Batching (fallback): All entities in one draw call, transforms on CPU

#include "assets/placement/PlacementExecutor.h"
#include "gl/GLBuffer.h"
#include "gl/GLVertexArray.h"
#include "primitives/InstanceData.h"
#include "vector/Tessellator.h"
#include "world/camera/WorldCamera.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"

#include <GL/glew.h>
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
	uint64_t frameCounter = 0;  // Incremented each render call (for LRU tracking)

	// --- LRU Cache Configuration ---
	// Keep recently-used chunks cached even when not visible, to avoid re-uploading
	// when panning back and forth. Only evict oldest when cache exceeds threshold.
	static constexpr size_t kMaxCachedChunks = 64;     // Max chunks to keep in cache
	static constexpr size_t kEvictionBatchSize = 8;    // How many to evict when over limit

	// --- Instancing Mode ---
	bool m_useInstancing = true;

	// Cache for GPU mesh handles (keyed by defName)
	// These hold the SHARED mesh geometry (VBO/IBO) that all chunks reference
	std::unordered_map<std::string, Renderer::InstancedMeshHandle> m_meshHandles;

	// Per-frame instance batches (grouped by mesh type, reused each frame)
	// Used ONLY for dynamic entities that change per-frame
	std::unordered_map<std::string, std::vector<Renderer::InstanceData>> m_instanceBatches;

	// --- Per-Chunk Instance Caching (Static Entities) ---
	// Upload instance data ONCE per chunk when first rendered, reuse every frame.
	// This eliminates the 22MB/frame CPU→GPU upload for static flora.

	/// GPU resources for a single mesh type within a chunk.
	/// The VAO references the shared mesh VBO/IBO but has its own instance VBO.
	/// Uses RAII wrappers for automatic GPU resource cleanup.
	struct CachedMeshData {
		Renderer::GLVertexArray vao;		 // VAO with shared mesh + chunk-specific instance buffer
		Renderer::GLBuffer instanceVBO;		 // Per-chunk instance buffer (GL_STATIC_DRAW)
		uint32_t instanceCount = 0;			 // Number of instances for draw call
		uint32_t indexCount = 0;			 // Index count from mesh (for draw call)
	};

	/// Per-chunk cached GPU resources for all mesh types in that chunk.
	struct ChunkInstanceCache {
		std::unordered_map<std::string, CachedMeshData> meshes;
		uint32_t totalEntityCount = 0;
		uint64_t lastAccessFrame = 0;  // Frame number when last rendered (for LRU eviction)
	};

	/// Cache of per-chunk instance data, keyed by chunk coordinate.
	std::unordered_map<ChunkCoordinate, ChunkInstanceCache> m_chunkInstanceCache;

	// --- Baked Static Mesh Path (replaces instancing for static entities) ---
	// Pre-transforms all entity vertices on CPU once at chunk load time.
	// Draws with glDrawElements instead of glDrawElementsInstanced.
	// Much faster for 100K+ entities due to sequential memory access.

	/// GPU resources for a chunk's baked entity mesh.
	/// All entities in the chunk are combined into a single VBO/IBO.
	struct BakedChunkData {
		Renderer::GLVertexArray vao;     // VAO with baked vertex data
		Renderer::GLBuffer vertexVBO;    // Pre-transformed vertices (world-space)
		Renderer::GLBuffer indexIBO;     // Combined indices
		uint32_t vertexCount = 0;        // Total vertices in VBO
		uint32_t indexCount = 0;         // Total indices in IBO
		uint32_t entityCount = 0;        // For debugging/metrics
		uint64_t lastAccessFrame = 0;    // For LRU eviction
	};

	/// Cache of baked per-chunk meshes.
	std::unordered_map<ChunkCoordinate, BakedChunkData> m_bakedChunkCache;

	/// Cached uniform locations for instanced rendering (avoid glGetUniformLocation per frame).
	struct CachedUniformLocations {
		GLint projection = -1;
		GLint transform = -1;
		GLint instanced = -1;
		GLint cameraPosition = -1;
		GLint cameraZoom = -1;
		GLint pixelsPerMeter = -1;
		GLint viewportSize = -1;
		bool initialized = false;
	};
	CachedUniformLocations m_uniformLocations;

	/// Initialize cached uniform locations from shader program.
	void initUniformLocations(GLuint shaderProgram);

	/// Build cached VAO + instance data for a chunk (called once per chunk).
	/// Creates per-chunk VAOs that reference shared mesh VBOs but have their own instance VBOs.
	void buildChunkCache(const assets::PlacementExecutor& executor, const ChunkCoordinate& coord);

	/// Release GPU resources for a chunk (called when chunk is unloaded).
	void releaseChunkCache(const ChunkCoordinate& coord);

	/// Render static entities using per-chunk cached VAOs (no per-frame upload).
	void renderCachedChunks(
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const WorldCamera& camera,
		int viewportWidth,
		int viewportHeight
	);

	// --- Baked Static Mesh Methods ---

	/// Build baked mesh for a chunk (pre-transform all entity vertices on CPU).
	/// Called once per chunk when first rendered.
	void buildBakedChunkMesh(const assets::PlacementExecutor& executor, const ChunkCoordinate& coord);

	/// Release baked mesh GPU resources for a chunk.
	void releaseBakedChunkCache(const ChunkCoordinate& coord);

	/// Render static entities using baked per-chunk meshes (glDrawElements, no instancing).
	void renderBakedChunks(
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const WorldCamera& camera,
		int viewportWidth,
		int viewportHeight
	);

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
