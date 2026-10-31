#pragma once

// The right-hand detail pane: a header (category kicker, name, type badge), then two columns. Left, a
// large preview: one framed thumbnail for a simple asset, or a 2x3 grid of seeded variants under a
// Randomize button for a procedural one (each cell a different generator seed), the collider drawn over
// the first. Right, the asset's facts, its validation issues, and its source XML in a scrolling well.

#include "AssetThumbnail.h"

#include <ui/Component.h>

#include <array>
#include <cstdint>
#include <string>

namespace asset_manager {

	class AssetDetailView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		explicit AssetDetailView(const Args& args);

		void setAsset(const std::string& defName); // empty shows a placeholder

		[[nodiscard]] const char* typeName() const override { return "AssetDetailView"; }

	  protected:
		void onMount() override;

	  private:
		void build();
		void preview(ui::Ref<ui::Component> column);
		void info(ui::Ref<ui::Component> column);
		void applyVariantSeeds();
		void randomize(); // advance to the next set of procedural variant seeds

		static constexpr int kVariants = 6;

		std::string									   defName;
		bool										   procedural = false;
		uint32_t									   variantBase = 1U;
		ui::Ref<ui::Component>						   content;
		std::array<ui::Ref<AssetThumbnail>, kVariants> variants;
	};

} // namespace asset_manager
