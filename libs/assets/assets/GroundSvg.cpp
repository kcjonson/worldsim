#include "assets/GroundSvg.h"

#include <vector/SVGLoader.h>

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <system_error>

namespace assets {

	namespace {

		// A tenth of a mip 0 texel: a flattened curve strays from the true one by less than a texel shows.
		constexpr float kCurveToleranceTexels = 0.1F;

		// The elements NanoSVG draws, each into one shape at most, and ones an author might expect drawn that it skips.
		constexpr std::array<std::string_view, 7> kDrawables = {"path", "rect", "circle", "ellipse", "line", "polyline", "polygon"};
		constexpr std::array<std::string_view, 3> kUndrawn = {"use", "text", "image"};
		// Containers NanoSVG draws the content of as plain shapes, or doesn't apply, wherever they sit.
		constexpr std::array<std::string_view, 6> kUnsupported = {"symbol", "clipPath", "mask", "pattern", "marker", "filter"};

		/// The text as a finite number, surrounding whitespace allowed.
		std::optional<float> parseNumber(const char* text) {
			char*		end = nullptr;
			const float value = std::strtof(text, &end);
			if (end == text) {
				return std::nullopt;
			}
			while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
				++end;
			}
			if (*end != '\0' || !std::isfinite(value)) {
				return std::nullopt;
			}
			return value;
		}

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
		bool setsProperty(pugi::xml_node node, std::string_view property) {
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

		/// A viewBox's four numbers, separated by whitespace or commas.
		std::optional<std::array<float, 4>> parseViewBox(const char* text) {
			std::array<float, 4> box{};
			const auto			 separator = [](char c) {
				  return c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r';
			};
			const char* at = text;
			for (float& value : box) {
				while (separator(*at)) {
					++at;
				}
				char* end = nullptr;
				value = std::strtof(at, &end);
				if (end == at || !std::isfinite(value)) {
					return std::nullopt;
				}
				at = end;
			}
			while (separator(*at)) {
				++at;
			}
			return *at == '\0' ? std::optional(box) : std::nullopt;
		}

		bool sameLength(float a, float b) {
			return std::abs(a - b) <= 1.0e-5F * std::max(1.0F, std::abs(b));
		}

		/// Resolves every element's data-lod and data-height down the tree, and tags each drawable element
		/// with its index into `shapes` as its id, which NanoSVG keeps on the shape it draws from it.
		class Walk {
		  public:
			struct Shape {
				float				 minPixelsPerMeter = 0.0F;
				std::optional<float> height;
				std::string			 where;			  // the element as its author finds it
				bool				 defined = false; // under <defs>, which NanoSVG never draws
			};

			Walk(std::span<const std::byte> newSource, std::vector<std::string>& newIssues)
				: source(newSource),
				  issues(newIssues) {}

			void visit(pugi::xml_node node, float minPixelsPerMeter, std::optional<float> height, bool defined) {
				if (const pugi::xml_attribute lod = node.attribute("data-lod")) {
					const std::optional<float> value = parseNumber(lod.value());
					if (!value.has_value() || *value < 0.0F) {
						issues.push_back(
							where(node) + ": data-lod=\"" + lod.value() + "\" must be a number of pixels per meter, 0 or more"
						);
					} else {
						minPixelsPerMeter = std::max(minPixelsPerMeter, *value);
					}
				}
				if (const pugi::xml_attribute own = node.attribute("data-height")) {
					const std::optional<float> value = parseNumber(own.value());
					if (!value.has_value() || *value < 0.0F || *value > 1.0F) {
						issues.push_back(where(node) + ": data-height=\"" + own.value() + "\" must be a number from 0 to 1");
					} else {
						height = value;
					}
				}
				const std::string_view name = node.name();
				defined = defined || name == "defs";
				if (!defined && std::find(kUndrawn.begin(), kUndrawn.end(), name) != kUndrawn.end()) {
					issues.push_back(where(node) + " is never drawn; ground art is paths and basic shapes");
				}
				if (name == "svg" && node.parent().type() != pugi::node_document) {
					issues.push_back(where(node) + " is a nested <svg>, which rescales the whole tile; ground art is one <svg>");
				}
				if (name == "style") {
					issues.push_back(where(node) + " is a style sheet, which the loader doesn't read; set each shape's fill on it");
				}
				if (std::find(kUnsupported.begin(), kUnsupported.end(), name) != kUnsupported.end()) {
					issues.push_back(where(node) + " isn't drawn the way a browser draws it; ground art is paths and basic shapes");
				}
				for (const char* ignored : {"clip-path", "mask", "filter"}) {
					if (setsProperty(node, ignored)) {
						issues.push_back(where(node) + " has a " + ignored + ", which the bake ignores");
					}
				}
				if (std::find(kDrawables.begin(), kDrawables.end(), name) != kDrawables.end()) {
					shapes.push_back({.minPixelsPerMeter = minPixelsPerMeter, .height = height, .where = where(node), .defined = defined});
					pugi::xml_attribute id = node.attribute("id");
					if (!id) {
						id = node.append_attribute("id");
					}
					id.set_value(std::to_string(shapes.size() - 1).c_str());
				}
				for (const pugi::xml_node child : node.children()) {
					if (child.type() == pugi::node_element) {
						visit(child, minPixelsPerMeter, height, defined);
					}
				}
			}

			std::vector<Shape> shapes;

		  private:
			// The element's tag, its id when it has one, and its line. Read before its id is replaced.
			std::string where(pugi::xml_node node) {
				const pugi::xml_attribute id = node.attribute("id");
				std::string out = std::string("<") + node.name() + (id ? std::string(" id=\"") + id.value() + "\"" : std::string()) + ">";
				const ptrdiff_t offset = node.offset_debug();
				if (offset < 0) {
					return out;
				}
				if (lineStarts.empty()) {
					lineStarts.push_back(0);
					for (size_t i = 0; i < source.size(); ++i) {
						if (source[i] == std::byte{'\n'}) {
							lineStarts.push_back(i + 1);
						}
					}
				}
				const auto line = std::upper_bound(lineStarts.begin(), lineStarts.end(), static_cast<size_t>(offset)) - lineStarts.begin();
				return out + " on line " + std::to_string(line);
			}

			std::span<const std::byte> source;
			std::vector<std::string>&  issues;
			std::vector<size_t>		   lineStarts;
		};

		struct TextWriter final : pugi::xml_writer {
			std::string text;
			void		write(const void* data, size_t size) override { text.append(static_cast<const char*>(data), size); }
		};

	} // namespace

	GeneratedAsset readGroundSvg(const GroundSvgArgs& args, std::vector<std::string>& issues) {
		GeneratedAsset out;

		pugi::xml_document			 doc;
		const pugi::xml_parse_result parsed = doc.load_buffer(args.svg.data(), args.svg.size());
		if (!parsed) {
			issues.push_back(std::string("not well-formed XML: ") + parsed.description() + ", at byte " + std::to_string(parsed.offset));
			return out;
		}
		pugi::xml_node root = doc.document_element();
		if (std::string_view(root.name()) != "svg") {
			issues.emplace_back("no <svg> root element");
			return out;
		}

		// NanoSVG scales the viewBox to the width and height; without them it keeps user units, which are meters.
		const pugi::xml_attribute				  viewBox = root.attribute("viewBox");
		const std::optional<std::array<float, 4>> box = parseViewBox(viewBox.value());
		if (!box.has_value() || (*box)[0] != 0.0F || (*box)[1] != 0.0F || !sameLength((*box)[2], args.periodMeters) ||
			!sameLength((*box)[3], args.periodMeters)) {
			char expected[64];
			std::snprintf(
				expected, sizeof(expected), "\"0 0 %g %g\"", static_cast<double>(args.periodMeters), static_cast<double>(args.periodMeters)
			);
			issues.push_back(
				(viewBox ? std::string("viewBox=\"") + viewBox.value() + "\" must be " : std::string("no viewBox; it must be ")) +
				expected + ", the def's period in meters"
			);
		}
		root.remove_attribute("width");
		root.remove_attribute("height");

		Walk walk(args.svg, issues);
		walk.visit(root, 0.0F, std::nullopt, false);

		TextWriter written;
		doc.save(written, "", pugi::format_raw | pugi::format_no_declaration);
		const float tolerance = kCurveToleranceTexels / static_cast<float>(std::max<uint32_t>(args.pixelsPerMeter, 1));
		std::vector<renderer::LoadedSVGShape> shapes;
		if (!renderer::loadSVG(std::as_bytes(std::span(written.text)), tolerance, shapes, args.name)) {
			issues.emplace_back("draws no shapes");
			return out;
		}

		std::vector<bool> drawn(walk.shapes.size(), false);
		for (renderer::LoadedSVGShape& shape : shapes) {
			const std::string& id = shape.id;
			size_t			   index = 0;
			const auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), index);
			if (error != std::errc{} || end != id.data() + id.size() || index >= walk.shapes.size()) {
				issues.push_back("a shape the reader can't trace to its element (id \"" + id + "\")");
				continue;
			}
			drawn[index] = true;
			const Walk::Shape& source = walk.shapes[index];
			const bool		   gradient = shape.gradient.type != renderer::GradientFill::Type::None;
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
			path.height = source.height;
			out.addPath(std::move(path));
		}
		// NanoSVG drops a shape whose fill it can't resolve (a pattern, a dangling url()) without a word.
		for (size_t i = 0; i < walk.shapes.size(); ++i) {
			if (!drawn[i] && !walk.shapes[i].defined) {
				issues.push_back(walk.shapes[i].where + " draws nothing; give it a fill color");
			}
		}
		return out;
	}

} // namespace assets
