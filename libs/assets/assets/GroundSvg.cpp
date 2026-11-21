#include "assets/GroundSvg.h"

#include "assets/LodSvg.h"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>

namespace assets {

	namespace {

		// A tenth of a mip 0 texel: a flattened curve strays from the true one by less than a texel shows.
		constexpr float kCurveToleranceTexels = 0.1F;

		// Elements an author might expect drawn that NanoSVG skips.
		constexpr std::array<std::string_view, 3> kUndrawn = {"use", "text", "image"};
		// Containers NanoSVG draws the content of as plain shapes, or doesn't apply, wherever they sit.
		constexpr std::array<std::string_view, 6> kUnsupported = {"symbol", "clipPath", "mask", "pattern", "marker", "filter"};

		std::string_view trimmed(std::string_view text) {
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
				text.remove_prefix(1);
			}
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
				text.remove_suffix(1);
			}
			return text;
		}

		// Whether an element sets `property` to anything but none, as an attribute or in its inline style:
		// moving a property into style="" mustn't slip it past the check.
		bool setsProperty(const pugi::xml_node& node, std::string_view property) {
			if (const pugi::xml_attribute attribute = node.attribute(std::string(property).c_str())) {
				return trimmed(attribute.value()) != "none";
			}
			std::string_view style = node.attribute("style").value();
			while (!style.empty()) {
				const size_t		   end = std::min(style.find(';'), style.size());
				const std::string_view declaration = style.substr(0, end);
				style.remove_prefix(std::min(end + 1, style.size()));
				const size_t colon = declaration.find(':');
				if (colon != std::string_view::npos && trimmed(declaration.substr(0, colon)) == property &&
					trimmed(declaration.substr(colon + 1)) != "none") {
					return true;
				}
			}
			return false;
		}

		bool sameLength(float a, float b) {
			return std::abs(a - b) <= 1.0e-5F * std::max(1.0F, std::abs(b));
		}

		/// An element's own data-height when it is a number from 0 to 1.
		std::optional<float> ownHeight(const pugi::xml_node& node) {
			const pugi::xml_attribute own = node.attribute("data-height");
			if (!own) {
				return std::nullopt;
			}
			const std::optional<float> value = parseSvgNumber(own.value());
			return value.has_value() && *value >= 0.0F && *value <= 1.0F ? value : std::nullopt;
		}

	} // namespace

	GeneratedAsset readGroundSvg(const GroundSvgArgs& args, std::vector<std::string>& issues) {
		GeneratedAsset out;

		// Each drawable element's data-height: its own, else its nearest ancestor's, else unset.
		std::vector<std::optional<float>> heights;
		const auto						  checkElement = [&](const pugi::xml_node& node, const std::string& where, std::optional<size_t> drawable) {
			 if (const pugi::xml_attribute own = node.attribute("data-height"); own && !ownHeight(node).has_value()) {
				 issues.push_back(where + ": data-height=\"" + own.value() + "\" must be a number from 0 to 1");
			 }
			 const std::string_view name = node.name();
			 bool					defined = false;
			 for (pugi::xml_node ancestor = node; ancestor; ancestor = ancestor.parent()) {
				 defined = defined || std::string_view(ancestor.name()) == "defs";
			 }
			 if (!defined && std::find(kUndrawn.begin(), kUndrawn.end(), name) != kUndrawn.end()) {
				 issues.push_back(where + " is never drawn; ground art is paths and basic shapes");
			 }
			 if (name == "svg" && node.parent().type() != pugi::node_document) {
				 issues.push_back(where + " is a nested <svg>, which rescales the whole tile; ground art is one <svg>");
			 }
			 if (name == "style") {
				 issues.push_back(where + " is a style sheet, which the loader doesn't read; set each shape's fill on it");
			 }
			 if (std::find(kUnsupported.begin(), kUnsupported.end(), name) != kUnsupported.end()) {
				 issues.push_back(where + " isn't drawn the way a browser draws it; ground art is paths and basic shapes");
			 }
			 for (const char* ignored : {"clip-path", "mask", "filter"}) {
				 if (setsProperty(node, ignored)) {
					 issues.push_back(where + " has a " + ignored + ", which the bake ignores");
				 }
			 }
			 if (drawable.has_value()) {
				 std::optional<float> height;
				 for (pugi::xml_node ancestor = node; ancestor && !height.has_value(); ancestor = ancestor.parent()) {
					 height = ownHeight(ancestor);
				 }
				 heights.push_back(height);
			 }
		};

		const float tolerance = kCurveToleranceTexels / static_cast<float>(std::max<uint32_t>(args.pixelsPerMeter, 1));
		LodSvg		svg = readLodSvg({.svg = args.svg, .curveTolerance = tolerance, .name = args.name, .onElement = checkElement}, issues);
		if (!svg.parsed) {
			return out;
		}

		// User units are meters: the viewBox is the tile.
		if (!svg.viewBox.has_value() || (*svg.viewBox)[0] != 0.0F || (*svg.viewBox)[1] != 0.0F ||
			!sameLength((*svg.viewBox)[2], args.periodMeters) || !sameLength((*svg.viewBox)[3], args.periodMeters)) {
			char expected[64];
			std::snprintf(
				expected, sizeof(expected), "\"0 0 %g %g\"", static_cast<double>(args.periodMeters), static_cast<double>(args.periodMeters)
			);
			issues.push_back(
				(svg.viewBoxText.has_value() ? "viewBox=\"" + *svg.viewBoxText + "\" must be " : std::string("no viewBox; it must be ")) +
				expected + ", the def's period in meters"
			);
		}

		for (LodSvgShape& loaded : svg.shapes) {
			renderer::LoadedSVGShape& shape = loaded.shape;
			const LodSvgElement&	  source = svg.elements[loaded.element];
			const bool				  gradient = shape.gradient.type != renderer::GradientFill::Type::None;
			if (shape.hasStroke) {
				issues.push_back(source.where + " has a stroke; a ground shape is filled, so convert the stroke to a path");
			}
			if (gradient) {
				issues.push_back(source.where + " fills with a gradient; a ground shape fills with one color");
			}
			if (shape.paths.size() != 1) {
				issues.push_back(
					source.where + " has " + std::to_string(shape.paths.size()) +
					" outlines; a ground shape is one, so break the path apart (the bake draws no holes)"
				);
			}
			if (!shape.hasFill || shape.hasStroke || gradient || shape.paths.size() != 1) {
				continue;
			}
			GeneratedPath path;
			path.vertices = std::move(shape.paths.front().vertices);
			path.fillColor = shape.fillColor;
			path.minPixelsPerMeter = source.minPixelsPerMeter;
			path.height = heights[loaded.element];
			out.addPath(std::move(path));
		}
		// NanoSVG drops a shape whose fill it can't resolve (a pattern, a dangling url()) without a word.
		for (const LodSvgElement& element : svg.elements) {
			if (!element.drawn && !element.defined && !svg.shapes.empty()) {
				issues.push_back(element.where + " draws nothing; give it a fill color");
			}
		}
		return out;
	}

} // namespace assets
