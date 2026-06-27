#include "AssetThumbnail.h"

#include <assets/AssetRegistry.h>
#include <assets/AssetRenderer.h>
#include <assets/MotionEval.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace asset_manager {

	namespace {
		// Mesh plus the raw (pre-fit) bounds it was built from, so the same fit
		// transform can be replayed for overlays (e.g. collision outlines).
		struct CachedMesh {
			renderer::TessellatedMesh mesh;
			Foundation::Rect		  sourceBounds{0.0F, 0.0F, 0.0F, 0.0F};
		};

		// Shared across all thumbnails so rebuilt rows reuse meshes (no re-tessellation). The
		// asset-manager is single-threaded -- reload runs synchronously on the UI loop, never
		// concurrent with render -- so this map and cacheGeneration() need no locking.
		std::unordered_map<std::string, CachedMesh>& meshCache() {
			static std::unordered_map<std::string, CachedMesh> cache;
			return cache;
		}

		// Bumped whenever meshCache is cleared. A thumbnail that fetched m_mesh at an older
		// generation must refetch before use -- the CachedMesh it points into is destroyed by
		// clearCache(), so drawing through the stale pointer is a use-after-free.
		uint64_t& cacheGeneration() {
			static uint64_t gen = 0;
			return gen;
		}

		// Wall-clock-driven walk phase [0,1) for the preview sweep, so an asset with a motion
		// animates continuously through its cycle. ~0.8 cycles/sec reads as a relaxed walk.
		float sweepPhase() {
			static const auto start = std::chrono::steady_clock::now();
			const float		  secs = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
			const float		  p = secs * 0.8F;
			return p - std::floor(p);
		}
	} // namespace

	void AssetThumbnail::clearCache() {
		meshCache().clear();
		++cacheGeneration();
	}

	void AssetThumbnail::setAsset(std::string defName, uint32_t seed) {
		if (defName != m_defName || seed != m_seed) {
			m_defName = std::move(defName);
			m_seed = seed;
			m_dirty = true;
		}
	}

	void AssetThumbnail::setSize(float width, float height) {
		if (width != m_size.x || height != m_size.y) {
			m_size = {width, height};
			m_dirty = true;
		}
	}

	void AssetThumbnail::ensureMesh() {
		// Refetch when dirty OR when the cache was cleared since our last fetch: clearCache()
		// frees the CachedMesh m_mesh points into, so a stale pointer is a use-after-free.
		const uint64_t gen = cacheGeneration();
		if (!m_dirty && m_cacheGen == gen) {
			return;
		}
		m_dirty = false;
		m_cacheGen = gen;
		m_mesh = nullptr;
		m_sourceBounds = {0.0F, 0.0F, 0.0F, 0.0F};
		m_targetRect = {0.0F, 0.0F, m_size.x, m_size.y};
		if (m_defName.empty()) {
			return;
		}

		const std::string key = m_defName + "@" + std::to_string(static_cast<int>(m_size.x)) + "x" +
								std::to_string(static_cast<int>(m_size.y)) + "#" + std::to_string(m_seed);
		auto& cache = meshCache();
		auto  it = cache.find(key);
		if (it == cache.end()) {
			engine::assets::PreparedAsset prepared = engine::assets::prepareAsset(m_defName, m_targetRect, m_seed);
			CachedMesh					  entry;
			entry.mesh = std::move(prepared.mesh);
			entry.sourceBounds = prepared.sourceBounds;
			it = cache.emplace(key, std::move(entry)).first;
		}
		m_mesh = &it->second.mesh;
		m_sourceBounds = it->second.sourceBounds;
	}

	bool AssetThumbnail::hasMesh() {
		ensureMesh();
		return m_mesh != nullptr && !m_mesh->vertices.empty();
	}

	Foundation::Vec2 AssetThumbnail::localToScreen(const glm::vec2& local) {
		ensureMesh();
		// Replay fitToRect (see renderer::fitToRect): scale to fit source bounds into
		// the target rect preserving aspect, center, then translate by the draw offset.
		const Foundation::Rect& src = m_sourceBounds;
		const Foundation::Rect& dst = m_targetRect;
		// Degenerate source: fitToRect leaves verts untransformed, so the overlay
		// must too -- raw local plus the draw offset, matching render().
		if (src.width <= 0.0F || src.height <= 0.0F) {
			return {local.x + m_pos.x, local.y + m_pos.y};
		}
		const float scale = std::min(dst.width / src.width, dst.height / src.height);
		const float srcCenterX = src.x + (src.width * 0.5F);
		const float srcCenterY = src.y + (src.height * 0.5F);
		const float dstCenterX = dst.x + (dst.width * 0.5F);
		const float dstCenterY = dst.y + (dst.height * 0.5F);
		const float fitX = dstCenterX + ((local.x - srcCenterX) * scale);
		const float fitY = dstCenterY + ((local.y - srcCenterY) * scale);
		// Mesh verts are translated by m_pos at draw; the overlay must match.
		return {fitX + m_pos.x, fitY + m_pos.y};
	}

	Foundation::Vec2 AssetThumbnail::centeredToScreen(const glm::vec2& local) {
		ensureMesh();
		const Foundation::Rect& src = m_sourceBounds;
		const Foundation::Rect& dst = m_targetRect;
		// Degenerate source: match localToScreen (raw local + draw offset, no centering).
		if (src.width <= 0.0F || src.height <= 0.0F) {
			return {local.x + m_pos.x, local.y + m_pos.y};
		}
		const float scale	   = std::min(dst.width / src.width, dst.height / src.height);
		const float dstCenterX = dst.x + (dst.width * 0.5F);
		const float dstCenterY = dst.y + (dst.height * 0.5F);
		// `local` is already relative to the mesh bbox center, which fitToRect maps to the
		// target-rect center -- so scale straight from there (no srcCenter subtraction,
		// which localToScreen does for raw mesh coords).
		return {dstCenterX + (local.x * scale) + m_pos.x, dstCenterY + (local.y * scale) + m_pos.y};
	}

	void AssetThumbnail::render() {
		if (!visible) {
			return;
		}
		ensureMesh();
		if (m_mesh == nullptr || m_mesh->vertices.empty()) {
			return;
		}

		// Reused across frames/instances so translating verts to m_pos doesn't allocate each frame.
		thread_local std::vector<Foundation::Vec2> verts;
		verts.assign(m_mesh->vertices.begin(), m_mesh->vertices.end());

		// Animate: if this asset declares a motion, sweep its first clip's phase over time and
		// deform the part vertex ranges in the fitted preview space, so the preview shows the
		// walk cycle in motion. Assets without a motion (the vast majority) skip this entirely.
		if (m_animated && !m_mesh->parts.empty()) {
			const auto* motion = engine::assets::AssetRegistry::Get().getMotion(m_defName);
			if (motion != nullptr) {
				const engine::assets::MotionClip* walk = motion->findClip("walk");
				const engine::assets::MotionClip* clip = (walk != nullptr) ? walk : &motion->clips.front();
				const Foundation::Rect&			  src = m_sourceBounds;
				const Foundation::Rect&			  dst = m_targetRect;
				if (!clip->drivers.empty() && src.width > 0.0F && src.height > 0.0F) {
					std::unordered_map<std::string, engine::assets::PartTransform> xforms;
					engine::assets::evaluateClip(*clip, sweepPhase(), xforms);

					const float		s = std::min(dst.width / src.width, dst.height / src.height);
					const glm::vec2 srcC{src.x + src.width * 0.5F, src.y + src.height * 0.5F};
					const glm::vec2 dstC{dst.x + dst.width * 0.5F, dst.y + dst.height * 0.5F};
					for (const auto& part : m_mesh->parts) {
						if (part.name.empty()) {
							continue;
						}
						auto it = xforms.find(part.name);
						if (it == xforms.end()) {
							continue;
						}
						// Convert pivot + translate from meter space into the fitted preview space
						// (rotation and scale are scale-invariant).
						engine::assets::PartTransform pt = it->second;
						pt.pivot	 = {dstC.x + (pt.pivot.x - srcC.x) * s, dstC.y + (pt.pivot.y - srcC.y) * s};
						pt.translate = {pt.translate.x * s, pt.translate.y * s};
						const uint32_t end =
							std::min<uint32_t>(part.vertexStart + part.vertexCount, static_cast<uint32_t>(verts.size()));
						for (uint32_t i = part.vertexStart; i < end; ++i) {
							const glm::vec2 r = pt.apply({verts[i].x, verts[i].y});
							verts[i] = {r.x, r.y};
						}
					}
				}
			}
		}

		for (auto& v : verts) {
			v.x += m_pos.x;
			v.y += m_pos.y;
		}
		Renderer::Primitives::drawTriangles({
			.vertices = verts.data(),
			.indices = m_mesh->indices.data(),
			.vertexCount = verts.size(),
			.indexCount = m_mesh->indices.size(),
			.color = Foundation::Color(0.7F, 0.7F, 0.7F, 1.0F),
			.colors = m_mesh->hasColors() ? m_mesh->colors.data() : nullptr,
		});
	}

} // namespace asset_manager
