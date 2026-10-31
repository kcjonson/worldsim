#include "scenes/game/ui/Look.h"

#include <ui/theme/Tokens.h>

namespace world_sim::look {

	ui::Style text(const ui::Theme& theme, ui::ColorToken color, ui::DimensionToken size, Font font) {
		const ui::FontFamilyToken family = font == Font::Mono	   ? ui::tokens::font::family::mono
										   : font == Font::Display ? ui::tokens::font::family::display
																   : ui::tokens::font::family::body;
		return {.color = theme.get(color), .fontFamily = theme.get(family), .fontSize = theme.get(size)};
	}

} // namespace world_sim::look
