#pragma once

// AssetThumbnail - a ui component that draws an asset through the game's render pipeline
// (assets::prepareAsset), fit to its own box. The one piece of custom rendering in the asset
// manager; everything else is salal's ui. With the collider shown, the asset's collision shape is drawn
// over the art, or "no collider" in the corner when it has none. A ground tile is framed on its square
// and clipped to it, built at the detail its square shows on screen, and with `tiles` above 1 it is drawn
// tiles x tiles, edge to edge, to show its seams. Meshes are cached across thumbnails, the least recently
// fetched dropped past a byte budget.

#include <graphics/Rect.h>
#include <ui/Component.h>
#include <vector/Types.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Renderer {
	class CoordinateSystem;
}

namespace asset_manager {

	struct ThumbnailMesh; // one asset's meshes at one size and seed, as the cache keeps them (AssetThumbnail.cpp)

	class AssetThumbnail : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string defName;
			uint32_t	seed = 42U;
			bool		animated = false; // sweep the asset's motion clip (the detail preview; the tree's stay still)
			bool		collider = false;
			int			tiles = 1; // a ground tile repeated tiles x tiles; other assets ignore it
			/// The app's, whose pixel ratio says how many device pixels a ground tile's square covers; 1 a
			/// logical pixel without one.
			const Renderer::CoordinateSystem* coordinateSystem = nullptr;
		};

		explicit AssetThumbnail(const Args& args);

		void setAsset(std::string defName, uint32_t seed = 42U);

		// Drop all cached meshes (call after an asset reload so thumbnails rebuild).
		static void clearCache();

		[[nodiscard]] const char* typeName() const override { return "AssetThumbnail"; }

	  protected:
		void render() override;

	  private:
		// Device pixels a ui unit: the ui's scale times the window's pixel ratio.
		[[nodiscard]] float pixelScale() const;
		void				ensureMesh();
		void				drawMeshes();
		void				drawCollider();
		// A point in raw asset-mesh space, or relative to the asset's bbox centre, in this box, by the
		// fitToRect math the preview applied to the mesh.
		[[nodiscard]] Foundation::Vec2 meshToLocal(const glm::vec2& point) const;
		[[nodiscard]] Foundation::Vec2 centeredToLocal(const glm::vec2& point) const;

		std::string							 defName;
		uint32_t							 seed;
		bool								 animated;
		bool								 collider;
		int									 tiles;
		const Renderer::CoordinateSystem*	 coordinateSystem;
		int									 repeat = 1;						   // tiles, once the asset turns out to be a ground tile
		std::shared_ptr<const ThumbnailMesh> mesh;								   // shared with the cache, which may drop it first
		Foundation::Rect					 sourceBounds{0.0F, 0.0F, 0.0F, 0.0F}; // framed region of the current asset
		Foundation::Rect					 targetRect{0.0F, 0.0F, 0.0F, 0.0F};   // the fit's target: this box, or one tile of it
		std::optional<Foundation::Rect>		 clip;								   // a ground tile's square in targetRect
		bool								 dirty = true;
		uint64_t							 cacheGen = 0; // the cache's generation when mesh was fetched; a reload since => refetch
		Foundation::Vec2					 fetchedSize{0.0F, 0.0F};
		float								 fetchedScale = 0.0F;
	};

} // namespace asset_manager
