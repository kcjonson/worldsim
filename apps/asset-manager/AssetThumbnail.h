#pragma once

// AssetThumbnail - a UI leaf that draws an asset through the game's render
// pipeline (engine::assets::prepareAsset), fit to its own bounds. The one piece
// of custom rendering in the asset manager; everything else uses libs/ui.

#include <component/Component.h>
#include <graphics/Rect.h>
#include <vector/Types.h>

#include <cstdint>
#include <string>

#include <glm/vec2.hpp>

namespace asset_manager {

	class AssetThumbnail : public UI::IComponent {
	  public:
		AssetThumbnail() = default;

		void setAsset(std::string defName, uint32_t seed = 42U);
		void setSize(float width, float height);

		// Drop all cached meshes (call after an asset reload so thumbnails rebuild).
		static void clearCache();

		void			   render() override;
		[[nodiscard]] float getWidth() const override { return m_size.x + margin; }
		[[nodiscard]] float getHeight() const override { return m_size.y + margin; }
		void				setPosition(float x, float y) override { m_pos = {x + margin, y + margin}; }

		// True once a mesh has been built and its fit transform is known.
		[[nodiscard]] bool hasMesh();

		// Map a point in asset-local space (the same space as the raw asset mesh and
		// the collision shape) to on-screen pixels, reusing the exact fitToRect math
		// the preview applied to the mesh. Call only when hasMesh() is true.
		[[nodiscard]] Foundation::Vec2 localToScreen(const glm::vec2& local);

	  private:
		void ensureMesh();

		std::string						 m_defName;
		uint32_t						 m_seed = 42U;
		Foundation::Vec2				 m_pos{0.0F, 0.0F};
		Foundation::Vec2				 m_size{32.0F, 32.0F};
		const renderer::TessellatedMesh* m_mesh = nullptr; // points into a shared cache; translated at draw
		Foundation::Rect				 m_sourceBounds{0.0F, 0.0F, 0.0F, 0.0F}; // raw mesh bounds for the current asset
		Foundation::Rect				 m_targetRect{0.0F, 0.0F, 0.0F, 0.0F};	 // fit target rect (pixel space)
		bool							 m_dirty = true;
	};

} // namespace asset_manager
