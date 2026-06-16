#pragma once

// Selectable font families. Each maps to a pre-generated MSDF atlas loaded by
// the FontRenderer at startup. Roboto is the default everywhere so existing
// callers that don't specify a family render exactly as before.
//
// Lives in the renderer layer (not ui) so both TextArgs here and FontRenderer
// in libs/ui can share the type without ui->renderer->ui dependency cycles.

namespace Renderer { // NOLINT(readability-identifier-naming)

	enum class FontFamily {
		Roboto,		   // Default UI body font
		ChakraPetch,   // Display / headings (tech-forward)
		Barlow,		   // Secondary UI font
		JetBrainsMono, // Monospace (numbers, code, data)
	};

	// Number of font families (for fixed-size storage in the renderer).
	constexpr int kFontFamilyCount = 4;

} // namespace Renderer
