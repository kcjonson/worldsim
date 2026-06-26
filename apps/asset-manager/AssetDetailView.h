#pragma once

// The right-hand detail pane: a top header (category kicker, name, type badge),
// then a two-column body. Left = a large preview: one framed thumbnail for a
// simple asset, or a 2x3 grid of seeded variants plus a Randomize button for a
// procedural one (each cell renders a different generator seed). The selected
// asset's collider is drawn over the first cell. Right = a compact info + source-
// XML column. Styled to the prototype tokens (see Theme.h).

#include "AssetThumbnail.h"

#include <components/button/Button.h>
#include <components/scroll/ScrollContainer.h>
#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <shapes/Shapes.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

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
		void applyVariantSeeds(); // push current seeds into the variant thumbnails
		void randomize();		  // advance to the next set of procedural variant seeds
		// Draw the selected asset's collision shape over one preview cell, aligned to the art.
		void drawCollisionOverlay(AssetThumbnail& thumb, const Foundation::Rect& card);

		Foundation::Rect					   m_bounds{0.0F, 0.0F, 0.0F, 0.0F};
		bool								   m_hasAsset = false;
		bool								   m_procedural = false;
		std::string							   m_defName;
		uint32_t							   m_variantBase = 1U;

		static constexpr int					 kVariants = 6;
		std::array<AssetThumbnail, kVariants>	 m_variants;	 // [0] alone for simple; all 6 for procedural
		std::array<Foundation::Rect, kVariants>	 m_cellFrames{}; // preview panel frames (left column)
		int										 m_cellCount = 1;	 // 1 (simple) or 6 (procedural)
		std::unique_ptr<UI::Button>				 m_randomize;

		UI::Text		  m_kicker;
		UI::Text		  m_name;
		UI::Text		  m_badge;
		Foundation::Color m_badgeFill{0.0F, 0.0F, 0.0F, 0.0F};
		Foundation::Color m_badgeBorder{0.0F, 0.0F, 0.0F, 0.0F};
		Foundation::Rect  m_badgeRect{0.0F, 0.0F, 0.0F, 0.0F};
		UI::Text		  m_meta;
		UI::Text		  m_warnings;
		UI::Text		  m_xmlHeader;
		UI::Text		  m_placeholder;

		std::unique_ptr<UI::ScrollContainer> m_xmlScroll;
		std::string							 m_xmlText;
		Foundation::Rect					 m_xmlWell{0.0F, 0.0F, 0.0F, 0.0F};

		static constexpr float kPad = 16.0F;
	};

} // namespace asset_manager
