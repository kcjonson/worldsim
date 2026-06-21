#include "AssetThumbnail.h"

#include <assets/AssetRenderer.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace asset_manager {

	namespace {
		// Shared across all thumbnails so rebuilt rows reuse meshes (no re-tessellation).
		std::unordered_map<std::string, renderer::TessellatedMesh>& meshCache() {
			static std::unordered_map<std::string, renderer::TessellatedMesh> cache;
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
		if (m_defName.empty()) {
			return;
		}

		const std::string key = m_defName + "@" + std::to_string(static_cast<int>(m_size.x)) + "x" +
								std::to_string(static_cast<int>(m_size.y)) + "#" + std::to_string(m_seed);
		auto& cache = meshCache();
		auto  it = cache.find(key);
		if (it == cache.end()) {
			engine::assets::PreparedAsset prepared = engine::assets::prepareAsset(m_defName, {0.0F, 0.0F, m_size.x, m_size.y}, m_seed);
			it = cache.emplace(key, std::move(prepared.mesh)).first;
		}
		m_mesh = &it->second;
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
