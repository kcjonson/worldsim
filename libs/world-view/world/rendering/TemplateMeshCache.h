#pragma once

// Memoizes AssetRegistry::getTemplate(templateId) pointers per renderer, so the
// registry's lock is taken once per template instead of per entity per frame.

#include <cstdint>
#include <vector>

namespace renderer {
struct TessellatedMesh;
}

namespace worldview {

/// Per-renderer cache of template meshes indexed by template id. The cached pointer
/// (including null) is the registry's, so behavior matches a direct lookup.
class TemplateMeshCache {
  public:
	const renderer::TessellatedMesh* get(uint32_t templateId);

  private:
	struct Slot {
		const renderer::TessellatedMesh* mesh = nullptr;
		bool							 resolved = false;
	};
	std::vector<Slot> m_slots;
};

}  // namespace worldview
