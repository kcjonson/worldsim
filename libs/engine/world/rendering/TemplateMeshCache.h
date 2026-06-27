#pragma once

// Memoizes AssetRegistry::getTemplate(defName) pointers per renderer, so the
// registry lookup happens once per defName instead of per entity per frame.

#include <string>
#include <unordered_map>

namespace renderer {
struct TessellatedMesh;
}

namespace engine::world {

/// Per-renderer cache of template meshes keyed by defName. The cached pointer
/// (including null) is the registry's, so behavior matches a direct lookup.
class TemplateMeshCache {
  public:
	const renderer::TessellatedMesh* get(const std::string& defName);

  private:
	std::unordered_map<std::string, const renderer::TessellatedMesh*> m_cache;
};

}  // namespace engine::world
