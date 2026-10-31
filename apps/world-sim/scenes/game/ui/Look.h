#pragma once

// The game ui's text on worldsim's tokens: a colour, a size and one of the theme's three families, the
// fields each of its Texts sets.

#include <theme/DesignTokens.h>

#include <ui/Style.h>
#include <ui/theme/Theme.h>

#include <cstdint>

namespace world_sim::look {

	namespace t = theme::tokens::theme;

	enum class Font : std::uint8_t { Body, Display, Mono };

	[[nodiscard]] ui::Style text(const ui::Theme& theme, ui::ColorToken color, ui::DimensionToken size, Font font = Font::Body);

} // namespace world_sim::look
