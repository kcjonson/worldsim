#include "AssetThumbnail.h"

#include <assets/AssetRenderer.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>

#include <algorithm>
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

		// Shared across all thumbnails so rebuilt rows reuse meshes (no re-tessellation).
		std::unordered_map<std::string, CachedMesh>& meshCache() {
			static std::unordered_map<std::string, CachedMesh> cache;
			return cache;
		}
	} // namespace

	void AssetThumbnail::clearCache() {
		meshCache().clear();
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
		if (!m_dirty) {
			return;
		}
		m_dirty = false;
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
