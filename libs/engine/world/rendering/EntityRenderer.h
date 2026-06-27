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
#include "world/rendering/BakedEntityMesh.h"
#include "primitives/InstanceData.h"
#include "vector/Tessellator.h"
#include "world/camera/WorldCamera.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"

#include <array>

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

	/// Draw call / triangle counts from last frame (raw GL draws bypass the
	/// BatchRenderer stats, so the metrics system reads them from here)
	[[nodiscard]] uint32_t lastDrawCallCount() const { return m_lastDrawCallCount; }
	[[nodiscard]] uint32_t lastTriangleCount() const { return m_lastTriangleCount; }

	/// Upload a CPU-baked chunk mesh (from a worker thread bake) to the GPU.
	/// Call on the render thread; replaces any existing baked data for the chunk.
	void uploadBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData);

	/// Queue a CPU-baked chunk mesh for budgeted upload (a few sub-chunks per
	/// frame, processed inside render). Avoids multi-ms frame spikes when
	/// several chunk bakes complete at once while scrolling.
	void queueBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData);

	/// Enable/disable GPU instancing (for A/B testing and fallback)
	void setInstancingEnabled(bool enabled) { m_useInstancing = enabled; }
	[[nodiscard]] bool isInstancingEnabled() const { return m_useInstancing; }

  private:
	float m_pixelsPerMeter = 16.0F;
	uint32_t m_lastEntityCount = 0;
	uint32_t m_lastDrawCallCount = 0;
	uint32_t m_lastTriangleCount = 0;
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

	// --- Baked Static Mesh Path with Sub-Chunk Culling ---
	// Entity vertices are pre-transformed to world space once per chunk (on a
	// worker thread via AsyncChunkProcessor, or on the render thread when an
	// evicted chunk is revisited). Sub-regions are culled against the viewport.
	// CPU bake types/constants live in BakedEntityMesh.h.

	/// GPU resources for one height bucket of one sub-region.
	struct BakedMeshGPU {
		Renderer::GLVertexArray vao;     // VAO with baked vertex data
		Renderer::GLBuffer vertexVBO;    // Pre-transformed vertices (world-space)
		Renderer::GLBuffer indexIBO;     // Combined indices
		uint32_t indexCount = 0;         // Total indices in IBO
		uint32_t entityCount = 0;        // For debugging/metrics
		float maxEntityHeight = 0.0F;    // Drives the far-zoom cutoff (short bucket)
	};

	/// GPU resources for a single sub-region's baked entity meshes.
	struct BakedSubChunkData {
		std::array<BakedMeshGPU, kFloraBucketCount> buckets;
		float minX = 0, minY = 0;        // World-space bounds for culling
		float maxX = 0, maxY = 0;
	};

	/// GPU resources for a chunk, subdivided into sub-regions.
	struct BakedChunkData {
		std::array<BakedSubChunkData, kSubChunkCount> subChunks;
		uint32_t totalEntityCount = 0;   // For debugging/metrics
		uint64_t lastAccessFrame = 0;    // For LRU eviction
	};

	/// Cache of baked per-chunk meshes.
	std::unordered_map<ChunkCoordinate, BakedChunkData> m_bakedChunkCache;

	// --- Groundcover (role==Groundcover) GPU-instanced path ---
	// Groundcover entities are SKIPPED by the baked path (their template lookup returns null
	// during bake) and drawn here instead. Each placed tuft is one instance of one of the
	// asset's procedural variant meshes (generated by AssetRegistry::buildMesh per seed, so
	// ALL look/feel lives in the asset), chosen by position hash. Dense grass costs a shared
	// mesh + per-instance data, and a tunable zoom LOD fades it out when blades shrink below a
	// few px on screen (the grass tile texture carries it from there).

	/// Per-chunk groundcover instances: defName -> per-variant instance buckets. Built lazily
	/// from the placement spatial index the first time a chunk is drawn.
	struct GroundcoverChunkCache {
		std::unordered_map<std::string, std::vector<std::vector<Renderer::InstanceData>>> byDef;
		float	 minX = 0, minY = 0, maxX = 0, maxY = 0; // world bounds for culling
		uint64_t lastAccessFrame = 0;
		bool	 built = false;
	};
	std::unordered_map<ChunkCoordinate, GroundcoverChunkCache> m_groundcoverChunkCache;

	/// Per-defName uploaded variant mesh handles, generated lazily from the asset
	/// (AssetRegistry::buildMesh per seed) so all look/feel stays in the asset, not engine C++.
	std::unordered_map<std::string, std::vector<Renderer::InstancedMeshHandle>> m_groundcoverHandles;

	/// Generate (once, lazily) and return a groundcover asset's variant mesh handles.
	const std::vector<Renderer::InstancedMeshHandle>& ensureGroundcoverVariants(const std::string& defName);

	/// Build one chunk's groundcover instance buckets from the spatial index.
	void buildGroundcoverChunk(const assets::PlacementExecutor& executor, const ChunkCoordinate& coord, GroundcoverChunkCache& cache);

	/// Draw groundcover for the visible chunks via GPU instancing, with the zoom LOD.
	void renderGroundcover(
		const assets::PlacementExecutor& executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const WorldCamera& camera,
		int viewportWidth,
		int viewportHeight
	);

	/// CPU bakes waiting for budgeted GPU upload (FIFO; partially uploaded
	/// chunks track progress via nextSubChunk)
	struct PendingUpload {
		ChunkCoordinate coord;
		BakedChunkCPUData cpuData;
		int nextSubChunk = 0;
	};
	std::vector<PendingUpload> m_pendingUploads;

	/// Max bytes of baked vertex/index data uploaded per frame. A byte budget
	/// (rather than a sub-chunk count) keeps the per-frame cost flat as flora
	/// density grows; 2MB is well under 1ms of PCIe transfer.
	static constexpr size_t kUploadBudgetBytesPerFrame = 2 * 1024 * 1024;

	/// Upload pending sub-chunks until the byte budget is exhausted (render thread)
	void processPendingUploads(size_t budgetBytes);

	/// Upload a single sub-chunk's buffers into an existing cache entry.
	/// @return Bytes of vertex+index data uploaded
	size_t uploadSubChunk(BakedChunkData& bakedData, BakedSubChunkCPUData& cpu, int subIndex);

	/// Cached uniform locations for instanced rendering (avoid glGetUniformLocation per frame).
	struct CachedUniformLocations {
		GLint projection = -1;
		GLint transform = -1;
		GLint instanced = -1;
		GLint cameraPosition = -1;
		GLint cameraZoom = -1;
		GLint pixelsPerMeter = -1;
		GLint viewportSize = -1;
		GLint bakedAlpha = -1;
		// Groundcover deform uniforms (instancing.glsl); set when drawing groundcover.
		GLint grassMode = -1;
		GLint grassOpenness = -1;
		GLint grassReach = -1;
		GLint cursorWorld = -1;
		GLint cursorRadius = -1;
		GLint cursorStrength = -1;
		bool initialized = false;
	};
	CachedUniformLocations m_uniformLocations;

	/// Initialize cached uniform locations from shader program.
	void initUniformLocations(GLuint shaderProgram);

	// --- Baked Static Mesh Methods ---

	/// Re-bake a chunk from the executor's spatial index on the render thread.
	/// Only used when a still-loaded chunk's baked mesh was LRU-evicted; new
	/// chunks arrive pre-baked via uploadBakedChunk.
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
