#pragma once

// Icon - SVG-based icon component
//
// Renders SVG assets with optional tinting at configurable sizes.
// Uses the renderer's SVG loading and tessellation pipeline.
//
// Pipeline:
//   1. loadSVG() - Parse SVG and flatten Bezier curves
//   2. Tessellator - Convert paths to triangles
//   3. drawTriangles() - Render with tint color

#include "component/Component.h"
#include "graphics/Color.h"
#include "theme/Theme.h"
#include "vector/Types.h"

#include <string>
#include <vector>

namespace UI {

class Icon : public Component {
  public:
	struct Args {
		Foundation::Vec2  position{0.0F, 0.0F};
		float			  size{Theme::Icons::defaultSize}; // Icon renders as size × size square
		std::string		  svgPath{};						// Path to SVG asset
		Foundation::Color tint{1.0F, 1.0F, 1.0F, 1.0F};		// Tint color (white = no tint)
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
	[[nodiscard]] const std::string&  getSvgPath() const { return svgPath; }
	[[nodiscard]] Foundation::Color	  getTint() const { return tint; }
	[[nodiscard]] float				  getIconSize() const { return iconSize; }
	[[nodiscard]] bool				  isLoaded() const { return !vertices.empty(); }

	// IComponent overrides
	void render() override;

	// Position update
	void setPosition(float x, float y) override;

  private:
	std::string		  svgPath;
	float			  iconSize;
	Foundation::Color tint;

	// Original SVG dimensions for scaling
	float originalWidth{0.0F};
	float originalHeight{0.0F};

	// Cached tessellated mesh
	std::vector<Foundation::Vec2> vertices;
	std::vector<uint16_t>		  indices;
	bool						  meshDirty{true};

	void rebuildMesh();
	void scaleVertices();
};

} // namespace UI
