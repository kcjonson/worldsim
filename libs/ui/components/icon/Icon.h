#pragma once

// Icon - vector icon component, two sources behind one widget.
//
//   * Salvage glyph: set Args::glyph to a registry name (see theme/IconGlyphs.h).
//     Stroked glyphs draw as round-joined line segments; filled glyphs tessellate.
//   * SVG asset: set Args::svgPath to load + tessellate an SVG file.
//
// glyph takes precedence when both are set. Geometry is built once (in the
// constructor / on size change) and reused each frame.

#include "component/Component.h"
#include "graphics/Color.h"
#include "theme/Theme.h"
#include "vector/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UI {

class Icon : public Component {
  public:
	struct Args {
		Foundation::Vec2  position{0.0F, 0.0F};
		float			  size{Theme::Icons::defaultSize}; // Icon renders as size × size square
		std::string		  svgPath{};						 // SVG asset path (used if glyph is empty)
		std::string		  glyph{};							 // Salvage glyph name (takes precedence)
		Foundation::Color tint{1.0F, 1.0F, 1.0F, 1.0F};		 // Tint color (stroke or fill)
		float			  strokeWidth{1.6F};				 // glyph stroke width in 24px icon space
		const char*		  id = nullptr;
		float			  margin{0.0F};
	};

	explicit Icon(const Args& args);
	~Icon() override = default;

	// Disable copy (owns mesh data)
	Icon(const Icon&) = delete;
	Icon& operator=(const Icon&) = delete;

	// Allow move
	Icon(Icon&&) noexcept = default;
	Icon& operator=(Icon&&) noexcept = default;

	// Value control
	void setSvgPath(const std::string& path);
	void setTint(Foundation::Color color);
	void setIconSize(float newSize);

	// Getters
	[[nodiscard]] const std::string& getSvgPath() const { return svgPath; }
	[[nodiscard]] Foundation::Color	 getTint() const { return tint; }
	[[nodiscard]] float				 getIconSize() const { return iconSize; }
	[[nodiscard]] bool				 isLoaded() const { return !originalVertices.empty() || !glyphStrokes.empty() || !glyphMeshVertices.empty(); }

	// IComponent overrides
	void render() override;

	// Position update
	void setPosition(float x, float y) override;

  private:
	std::string		  svgPath;
	std::string		  glyph;
	float			  iconSize;
	Foundation::Color tint;
	bool			  glyphMode{false};

	// --- SVG mode ---
	float						  originalWidth{0.0F};
	float						  originalHeight{0.0F};
	std::vector<Foundation::Vec2> originalVertices; // Unscaled vertices from tessellation
	std::vector<uint16_t>		  indices;
	std::vector<Foundation::Vec2> vertices; // Scaled/offset vertices ready for render

	// --- Salvage glyph mode ---
	bool						  glyphFilled{false};
	float						  glyphStrokeBase{1.6F}; // unscaled (24px space)
	float						  glyphStrokeWidth{1.6F}; // scaled to size
	std::vector<Foundation::Vec2> glyphMeshVertices;
	std::vector<uint16_t>		  glyphMeshIndices;
	struct Stroke {
		std::vector<Foundation::Vec2> points;
		bool						  closed{false};
	};
	std::vector<Stroke> glyphStrokes;

	void rebuildMesh();			 // SVG
	void applyScaleToVertices(); // SVG
	void buildGlyph();			 // Salvage glyph
};

} // namespace UI
