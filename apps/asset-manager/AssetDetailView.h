#pragma once

// The right-hand detail pane: a framed preview, a header (category kicker, name,
// type badge, metadata), per-asset validation warnings, and the source XML in a
// code well. Styled to the prototype tokens (see Theme.h). Content is stacked
// with measured heights so nothing overlaps.

#include "AssetThumbnail.h"

#include <components/scroll/ScrollContainer.h>
#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <shapes/Shapes.h>

#include <memory>
#include <string>

namespace engine::assets {
	struct AssetDefinition;
}

namespace asset_manager {

	class AssetDetailView {
	  public:
		AssetDetailView();

		void setBounds(const Foundation::Rect& bounds);
		void setAsset(const std::string& defName); // empty string shows a placeholder
		void render();
		bool handleInput(UI::InputEvent& event);
		void update(float dt);

	  private:
		void relayout();
		// Draw the selected asset's collision shape over the preview, aligned to the art.
		void drawCollisionOverlay(const Foundation::Rect& previewCard);

		Foundation::Rect					 m_bounds{0.0F, 0.0F, 0.0F, 0.0F};
		bool								 m_hasAsset = false;
		const engine::assets::AssetDefinition* m_def = nullptr; // current asset (for the collision overlay)
		AssetThumbnail						 m_preview;
		UI::Text							 m_kicker;
		UI::Text							 m_name;
		UI::Text							 m_badge;
		Foundation::Color					 m_badgeFill{0.0F, 0.0F, 0.0F, 0.0F};
		Foundation::Color					 m_badgeBorder{0.0F, 0.0F, 0.0F, 0.0F};
		Foundation::Rect					 m_badgeRect{0.0F, 0.0F, 0.0F, 0.0F};
		UI::Text							 m_meta;
		UI::Text							 m_warnings;
		UI::Text							 m_xmlHeader;
		UI::Text							 m_placeholder;
		std::unique_ptr<UI::ScrollContainer> m_xmlScroll;
		float								 m_dividerY = 0.0F;

		static constexpr float kPad = 16.0F;
		static constexpr float kPreview = 300.0F;
	};

} // namespace asset_manager
