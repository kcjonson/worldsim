#include "AssetGridView.h"

#include <assets/AssetRenderer.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>

#include <algorithm>
#include <utility>

namespace asset_manager {

	void AssetGridView::setItems(std::vector<std::string> defNames) {
		m_items = std::move(defNames);
		m_meshCache.clear();
	}

	const renderer::TessellatedMesh& AssetGridView::cellMesh(const std::string& defName) {
		auto it = m_meshCache.find(defName);
		if (it != m_meshCache.end()) {
			return it->second;
		}
		// Fit the asset into a thumbnail-sized box at the origin (seed fixed so the
		// gallery is stable); translated into each cell at draw time.
		const Foundation::Rect		  box{0.0F, 0.0F, kThumb, kThumb};
		engine::assets::PreparedAsset prepared = engine::assets::prepareAsset(defName, box, 42U);
		auto						  inserted = m_meshCache.emplace(defName, std::move(prepared.mesh));
		return inserted.first->second;
	}

	void AssetGridView::render() {
		if (m_items.empty() || m_bounds.width <= 0.0F) {
			return;
		}

		const int	cols = std::max(1, static_cast<int>(m_bounds.width / kCellW));
		const float viewBottom = m_bounds.y + m_bounds.height;

		for (size_t i = 0; i < m_items.size(); ++i) {
			const int col = static_cast<int>(i) % cols;
			const int row = static_cast<int>(i) / cols;

			const float cellX = m_bounds.x + (static_cast<float>(col) * kCellW) + kPadding;
			const float cellY = m_bounds.y + (static_cast<float>(row) * kCellH) + kPadding;

			if (cellY > viewBottom) {
				break; // rows below the viewport: nothing further is visible
			}

			const renderer::TessellatedMesh& mesh = cellMesh(m_items[i]);
			if (!mesh.vertices.empty()) {
				std::vector<Foundation::Vec2> verts = mesh.vertices;
				for (auto& v : verts) {
					v.x += cellX;
					v.y += cellY;
				}
				Renderer::Primitives::drawTriangles({
					.vertices = verts.data(),
					.indices = mesh.indices.data(),
					.vertexCount = verts.size(),
					.indexCount = mesh.indices.size(),
					.color = Foundation::Color(0.7F, 0.7F, 0.7F, 1.0F),
					.colors = mesh.hasColors() ? mesh.colors.data() : nullptr,
				});
			}

			Renderer::Primitives::drawText({
				.text = m_items[i],
				.position = {cellX, cellY + kThumb + 4.0F},
				.scale = 0.7F,
				.color = Foundation::Color(0.85F, 0.85F, 0.85F, 1.0F),
			});
		}
	}

} // namespace asset_manager
