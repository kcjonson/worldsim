#include "TemplateMeshCache.h"

#include "assets/AssetRegistry.h"

namespace worldview {

	const renderer::TessellatedMesh* TemplateMeshCache::get(uint32_t templateId) {
		if (templateId >= m_slots.size()) {
			m_slots.resize(static_cast<size_t>(templateId) + 1);
		}
		Slot& slot = m_slots[templateId];
		if (!slot.resolved) {
			slot.mesh = assets::AssetRegistry::Get().getTemplate(templateId);
			slot.resolved = true;
		}
		return slot.mesh;
	}

}  // namespace worldview
