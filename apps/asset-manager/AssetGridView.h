#pragma once

// A wall of asset thumbnails. Each thumbnail is the asset drawn through the
// game's render pipeline (engine::assets::prepareAsset), cached once as a
// cell-sized mesh and translated into place. Rows below the viewport are culled.

#include <graphics/Rect.h>
#include <vector/Types.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace asset_manager {

	class AssetGridView {
	  public:
		void setItems(std::vector<std::string> defNames);
		void setBounds(const Foundation::Rect& bounds) { m_bounds = bounds; }
		void render();

	  private:
		// Lazily builds (and caches) the asset's mesh fit into a thumbnail-sized box
		// at the origin. Translated per cell at draw time.
		const renderer::TessellatedMesh& cellMesh(const std::string& defName);

		Foundation::Rect										   m_bounds{0.0F, 0.0F, 0.0F, 0.0F};
		std::vector<std::string>								   m_items;
		std::unordered_map<std::string, renderer::TessellatedMesh> m_meshCache;

		static constexpr float kCellW = 150.0F;
		static constexpr float kCellH = 168.0F;
		static constexpr float kThumb = 130.0F;
		static constexpr float kPadding = 10.0F;
	};

} // namespace asset_manager
