#include "AssetThumbnail.h"

#include <assets/AssetRenderer.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace asset_manager {

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

		// Shared across all thumbnails so rebuilt rows reuse meshes (no re-tessellation).
		static std::unordered_map<std::string, renderer::TessellatedMesh> s_cache;
		const std::string key = m_defName + "@" + std::to_string(static_cast<int>(m_size.x)) + "x" +
								std::to_string(static_cast<int>(m_size.y)) + "#" + std::to_string(m_seed);
		auto it = s_cache.find(key);
		if (it == s_cache.end()) {
			engine::assets::PreparedAsset prepared = engine::assets::prepareAsset(m_defName, {0.0F, 0.0F, m_size.x, m_size.y}, m_seed);
			it = s_cache.emplace(key, std::move(prepared.mesh)).first;
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

		std::vector<Foundation::Vec2> verts = m_mesh->vertices;
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
