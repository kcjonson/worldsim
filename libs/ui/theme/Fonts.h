#pragma once

// The game's font families and the atlas each one loads, kept together so a new
// family is one edit here (plus its TTF in the root CMakeLists.txt, whose
// font-atlas target makes the atlas). Each app's CMakeLists.txt copies fonts/
// beside its exe, and its main hands kFontAtlases and fontDefault to AppLauncher
// through AppConfig.

#include "font/FontRenderer.h"
#include "primitives/FontFamily.h"

#include <array>

namespace UI {

	// fontDefault is Roboto, what text that names no family is drawn and measured
	// with. The Salvage typeface roles: Display = titles/values/labels,
	// UI = body/units, Mono = kickers/numeric meters/badges/keycaps/dividers/eyebrows.
	inline constexpr Renderer::FontFamily fontDefault{0};
	inline constexpr Renderer::FontFamily fontDisplay{1};
	inline constexpr Renderer::FontFamily fontUi{2};
	inline constexpr Renderer::FontFamily fontMono{3};

	// FontFamily::Default stands for the default and isn't a family: the FontRenderer
	// refuses an id equal to it, which would stop every app at startup.
	static_assert(
		fontDefault != Renderer::FontFamily::Default && fontDisplay != Renderer::FontFamily::Default &&
		fontUi != Renderer::FontFamily::Default && fontMono != Renderer::FontFamily::Default
	);

	// Paths relative to the exe; AppLauncher finds them through findResource.
	inline const std::array<Renderer::FontAtlasSource, 4> kFontAtlases{{
		{.family = fontDefault, .pngPath = "fonts/Roboto-SDF.png", .jsonPath = "fonts/Roboto-SDF.json"},
		{.family = fontDisplay, .pngPath = "fonts/ChakraPetch-SDF.png", .jsonPath = "fonts/ChakraPetch-SDF.json"},
		{.family = fontUi, .pngPath = "fonts/Barlow-SDF.png", .jsonPath = "fonts/Barlow-SDF.json"},
		{.family = fontMono, .pngPath = "fonts/JetBrainsMono-SDF.png", .jsonPath = "fonts/JetBrainsMono-SDF.json"},
	}};

} // namespace UI
