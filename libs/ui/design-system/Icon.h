#pragma once

// Salvage design-system primitive: Icon.
//
// Renders one of the line-icon glyphs (see IconGlyphs.h, generated from
// icons.json). Stroked glyphs are drawn as line segments with round joins and
// caps; the eight filled glyphs are tessellated. Geometry is precomputed at the
// requested size in the constructor and reused each frame.
//
// Spec: docs/design/ui/design-system/icons.md.

#include "design-system/Tokens.h"
#include "graphics/Color.h"
#include "math/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UI::DS {

	class Icon {
	  public:
		struct Args {
			Foundation::Vec2  position{0.0F, 0.0F};
			std::string		  glyph;				// glyph name, e.g. "gear" (see icons.md)
			float			  size = 16.0F;			// icon box edge in px
			Foundation::Color color = text;			// tint (stroke or fill)
			float			  strokeWidth = 1.6F;	// in 24px icon space, scaled with size
		};

		explicit Icon(const Args& args);

		void render() const;

	  private:
		Foundation::Vec2  position;
		Foundation::Color color;
		bool			  filled = false;
		float			  strokeWidth = 1.6F; // already scaled to size

		// Filled glyphs: a single tessellated mesh at origin (position added at render).
		std::vector<Foundation::Vec2> meshVertices;
		std::vector<std::uint16_t>	  meshIndices;

		// Stroked glyphs: scaled polylines at origin.
		struct Stroke {
			std::vector<Foundation::Vec2> points;
			bool						  closed;
		};
		std::vector<Stroke> strokes;
	};

} // namespace UI::DS
