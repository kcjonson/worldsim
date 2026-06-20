#pragma once

// The right-hand detail pane: a large preview of the selected asset, its parsed
// metadata, and the source XML in a scrollable text block. Composed from libs/ui
// components plus the AssetThumbnail leaf.

#include "AssetThumbnail.h"

#include <components/scroll/ScrollContainer.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <shapes/Shapes.h>

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

		Foundation::Rect					 m_bounds{0.0F, 0.0F, 0.0F, 0.0F};
		bool								 m_hasAsset = false;
		AssetThumbnail						 m_preview;
		UI::Text							 m_name;
		UI::Text							 m_meta;
		UI::Text							 m_warnings;
		UI::Text							 m_xmlHeader;
		UI::Text							 m_placeholder;
		std::unique_ptr<UI::ScrollContainer> m_xmlScroll;

		static constexpr float kPad = 16.0F;
		static constexpr float kPreview = 180.0F;
	};

} // namespace asset_manager
