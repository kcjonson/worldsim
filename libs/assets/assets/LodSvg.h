#pragma once

// The data-lod attribute pass every vector drawing goes through on its way to salal's loadSVG
// (docs/technical/ground-textures.md, "LOD"): one pugixml walk resolves each element's effective
// data-lod, the largest on it and its ancestors, else 0, and carries it through loadSVG on the shape
// id, so each loaded shape knows the screen density it appears at. Ground tiles (GroundSvg.h) and
// parts drawings (PartsAsset.h) read their SVGs through it.

#include <vector/SVGLoader.h>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pugi {
	class xml_node;
}

namespace assets {

	/// One drawable element (path, rect, circle, ellipse, line, polyline, polygon) in document order.
	struct LodSvgElement {
		float		minPixelsPerMeter = 0.0F; ///< its effective data-lod
		std::string where;					  ///< the element as its author finds it: tag, id, and line
		bool		defined = false;		  ///< under <defs>, which is never drawn
		bool		drawn = false;			  ///< loadSVG made a shape of it
	};

	/// A loaded shape and the index of the element it was drawn from.
	struct LodSvgShape {
		renderer::LoadedSVGShape shape;
		std::size_t				 element = 0;
	};

	struct LodSvg {
		bool								parsed = false; ///< the text was well-formed XML with an <svg> root
		std::optional<std::string>			viewBoxText;	///< the viewBox attribute as written; unset when missing
		std::optional<std::array<float, 4>> viewBox;		///< min x, min y, width, height; unset when missing or malformed
		std::vector<LodSvgElement>			elements;
		std::vector<LodSvgShape>			shapes; ///< in viewBox units (width and height are dropped)
	};

	struct LodSvgArgs {
		std::span<const std::byte> svg;
		float					   curveTolerance = 1.0F; ///< in viewBox units
		std::string_view		   name;				  ///< what the log calls the SVG
		/// Called for every element, before its id is replaced, with where it is and, for a drawable
		/// element, its index in LodSvg::elements: the hook a reader adds its own checks on.
		std::function<void(const pugi::xml_node& element, const std::string& where, std::optional<std::size_t> drawable)> onElement;
	};

	/// An attribute's text as a finite number, surrounding whitespace allowed.
	[[nodiscard]] std::optional<float> parseSvgNumber(const char* text);

	/// Reads an SVG through the data-lod pass and loadSVG. Problems go in `issues`, one message each naming
	/// the element and its line: not well-formed XML, no <svg> root, a data-lod that isn't a number of 0 or
	/// more, a shape that can't be traced to its element, and an SVG that draws nothing.
	[[nodiscard]] LodSvg readLodSvg(const LodSvgArgs& args, std::vector<std::string>& issues);

} // namespace assets
