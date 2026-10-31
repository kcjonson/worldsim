#pragma once

// AssetThumbnail - a ui component that draws an asset through the game's render pipeline
// (engine::assets::prepareAsset), fit to its own box. The one piece of custom rendering in the asset
// manager; everything else is salal's ui. With the collider shown, the asset's collision shape is drawn
// over the art, or "no collider" in the corner when it has none.

#include <graphics/Rect.h>
#include <ui/Component.h>
#include <vector/Types.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>

namespace asset_manager {

	class AssetThumbnail : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string defName;
			uint32_t	seed = 42U;
			bool		animated = false; // sweep the asset's motion clip (the detail preview; the tree's stay still)
			bool		collider = false;
		};

		explicit AssetThumbnail(const Args& args);

		void setAsset(std::string defName, uint32_t seed = 42U);

		// Drop all cached meshes (call after an asset reload so thumbnails rebuild).
		static void clearCache();

		[[nodiscard]] const char* typeName() const override { return "AssetThumbnail"; }

	  protected:
		void render() override;

	  private:
		void ensureMesh();
		void drawCollider();
		// A point in raw asset-mesh space, or relative to the asset's bbox centre, in this box, by the
		// fitToRect math the preview applied to the mesh.
		[[nodiscard]] Foundation::Vec2 meshToLocal(const glm::vec2& point) const;
		[[nodiscard]] Foundation::Vec2 centeredToLocal(const glm::vec2& point) const;

		std::string						 defName;
		uint32_t						 seed;
		bool							 animated;
		bool							 collider;
		const renderer::TessellatedMesh* mesh = nullptr;					   // points into a shared cache
		Foundation::Rect				 sourceBounds{0.0F, 0.0F, 0.0F, 0.0F}; // raw mesh bounds for the current asset
		Foundation::Rect				 targetRect{0.0F, 0.0F, 0.0F, 0.0F};   // the fit's target: this box
		bool							 dirty = true;
		uint64_t						 cacheGen = 0; // meshCache generation mesh was fetched at; stale => refetch
	};

} // namespace asset_manager
