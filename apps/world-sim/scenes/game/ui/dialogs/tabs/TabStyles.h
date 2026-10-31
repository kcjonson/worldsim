#pragma once

// The colonist dossier tabs' text styles on worldsim's tokens: a label, muted copy, the small print, and
// the kicker, small upper-case mono tracked wide.

#include "scenes/game/ui/Look.h"

#include <graphics/PrimitiveStyles.h>

namespace world_sim::tabs {

	[[nodiscard]] inline ui::Style labelText(const ui::Theme& theme) {
		return look::text(theme, look::t::color::text, look::t::fontSize::fsSm);
	}

	[[nodiscard]] inline ui::Style mutedText(const ui::Theme& theme) {
		return look::text(theme, look::t::color::textDim, look::t::fontSize::fsSm);
	}

	[[nodiscard]] inline ui::Style smallText(const ui::Theme& theme) {
		return look::text(theme, look::t::color::textDim, look::t::fontSize::fs2xs);
	}

	[[nodiscard]] inline ui::Style kickerText(const ui::Theme& theme, ui::ColorToken color) {
		const float size = theme.get(look::t::fontSize::fs2xs);
		return look::text(theme, color, look::t::fontSize::fs2xs, look::Font::Mono)
			.extend({
				.letterSpacing = size * theme.get(look::t::letterSpacing::lsWider),
				.textTransform = Foundation::TextTransform::Uppercase,
			});
	}

} // namespace world_sim::tabs
