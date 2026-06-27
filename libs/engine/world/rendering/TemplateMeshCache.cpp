#include "TemplateMeshCache.h"

#include "assets/AssetRegistry.h"

namespace engine::world {

	const renderer::TessellatedMesh* TemplateMeshCache::get(const std::string& defName) {
		auto it = m_cache.find(defName);
		if (it != m_cache.end()) {
			return it->second;
		}
		const auto* mesh = assets::AssetRegistry::Get().getTemplate(defName);
		m_cache[defName] = mesh;
		return mesh;
	}

}  // namespace engine::world
