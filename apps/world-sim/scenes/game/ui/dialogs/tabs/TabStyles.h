#pragma once

// The colonist dossier tabs' text styles on worldsim's tokens: a label, muted copy, the small print, and
// the kicker, small upper-case mono tracked wide.

#include "scenes/shared/SalalUi.h"

#include <graphics/PrimitiveStyles.h>

namespace world_sim::tabs {

	[[nodiscard]] inline ui::Style labelText(const ui::Theme& theme) {
		return world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::text});
	}

	[[nodiscard]] inline ui::Style mutedText(const ui::Theme& theme) {
		return world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textDim});
	}

	[[nodiscard]] inline ui::Style smallText(const ui::Theme& theme) {
		return world_sim::text(theme, {.size = tokens::fontSize::fs2xs, .color = tokens::color::textDim});
	}

	[[nodiscard]] inline ui::Style kickerText(const ui::Theme& theme, ui::ColorToken color) {
		const float size = theme.get(tokens::fontSize::fs2xs);
		return world_sim::text(theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fs2xs, .color = color})
			.extend({
				.letterSpacing = size * theme.get(tokens::letterSpacing::lsWider),
				.textTransform = Foundation::TextTransform::Uppercase,
			});
	}

} // namespace world_sim::tabs
