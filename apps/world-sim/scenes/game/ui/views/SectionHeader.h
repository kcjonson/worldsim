#pragma once

// SectionHeader - a section's title in a column of content: one line of text, dim and left aligned, in
// a size and colour from the theme's tokens. Its style's set fields win over both.

#include "scenes/shared/SalalUi.h"

#include <ui/shapes/Text.h>

#include <string>

namespace world_sim {

	class SectionHeader : public ui::Text {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string		   text;
			ui::DimensionToken fontSize = tokens::fontSize::fsSm;
			ui::ColorToken	   color = tokens::color::textDim;
		};

		explicit SectionHeader(const Args& args);

		[[nodiscard]] const char* typeName() const override { return "SectionHeader"; }

	  protected:
		void onMount() override;

	  private:
		ui::DimensionToken fontSize;
		ui::ColorToken	   color;
	};

} // namespace world_sim
