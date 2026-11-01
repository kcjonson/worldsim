#pragma once

// Worldsim's theme on a ui context: its tokens over ui's, ui's font roles (display, body, mono) named
// for worldsim's families, and its icons. A scene applies it to its ui() in onEnter; a debug layer to
// the context it owns.

#include <ui/Context.h>

#include <span>

namespace theme {

	void apply(ui::Context& context);

	// ui's font roles named for worldsim's families, as apply loads the theme with them.
	std::span<const ui::FontFamilyName> fontFamilyNames();

} // namespace theme
