#pragma once

// SectionHeader - Styled section title for organizing content.
//
// A simple text component styled as a section header:
// - Uses theme header color by default
// - Left aligned
// - Configurable font size
//
// Implements IComponent for use in LayoutContainer.

#include "shapes/Shapes.h"
#include "theme/Theme.h"

#include <string>

namespace UI {

/// A styled section header for organizing content
class SectionHeader : public Text {
  public:
	struct Args {
		std::string text;
		float fontSize = 12.0F;
		Foundation::Color color = Theme::Colors::textHeader;
		float margin = 0.0F;
		std::string id = "section_header";
	};

	explicit SectionHeader(const Args& args)
		: Text(Text::Args{
			  .position = {0.0F, 0.0F},
			  .text = args.text,
			  .style = {
				  .color = args.color,
				  .fontSize = args.fontSize,
				  .hAlign = Foundation::HorizontalAlign::Left,
				  .vAlign = Foundation::VerticalAlign::Top,
			  },
			  .id = args.id.c_str()
		  }) {
		margin = args.margin;
	}
};

} // namespace UI
