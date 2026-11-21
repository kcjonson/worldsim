#include "assets/LodSvg.h"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <system_error>

namespace assets {

	namespace {

		// The elements NanoSVG draws, each into one shape at most.
		constexpr std::array<std::string_view, 7> kDrawables = {"path", "rect", "circle", "ellipse", "line", "polyline", "polygon"};

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

		class Walk {
		  public:
			Walk(const LodSvgArgs& newArgs, std::vector<std::string>& newIssues)
				: args(newArgs),
				  issues(newIssues) {}

			void visit(pugi::xml_node node, float minPixelsPerMeter, bool defined) {
				const std::string location = where(node);
				if (const pugi::xml_attribute lod = node.attribute("data-lod")) {
					const std::optional<float> value = parseSvgNumber(lod.value());
					if (!value.has_value() || *value < 0.0F) {
						issues.push_back(location + ": data-lod=\"" + lod.value() + "\" must be a number of pixels per meter, 0 or more");
					} else {
						minPixelsPerMeter = std::max(minPixelsPerMeter, *value);
					}
				}
				const std::string_view name = node.name();
				defined = defined || name == "defs";
				const bool				   drawable = std::find(kDrawables.begin(), kDrawables.end(), name) != kDrawables.end();
				std::optional<std::size_t> index;
				if (drawable) {
					index = elements.size();
					elements.push_back({.minPixelsPerMeter = minPixelsPerMeter, .where = location, .defined = defined});
				}
				if (args.onElement) {
					args.onElement(node, location, defined, index);
				}
				if (index.has_value()) {
					pugi::xml_attribute id = node.attribute("id");
					if (!id) {
						id = node.append_attribute("id");
					}
					id.set_value(std::to_string(*index).c_str());
				}
				for (const pugi::xml_node child : node.children()) {
					if (child.type() == pugi::node_element) {
						visit(child, minPixelsPerMeter, defined);
					}
				}
			}

			std::vector<LodSvgElement> elements;

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
					for (size_t i = 0; i < args.svg.size(); ++i) {
						if (args.svg[i] == std::byte{'\n'}) {
							lineStarts.push_back(i + 1);
						}
					}
				}
				const auto line = std::upper_bound(lineStarts.begin(), lineStarts.end(), static_cast<size_t>(offset)) - lineStarts.begin();
				return out + " on line " + std::to_string(line);
			}

			const LodSvgArgs&		  args;
			std::vector<std::string>& issues;
			std::vector<size_t>		  lineStarts;
		};

		struct TextWriter final : pugi::xml_writer {
			std::string text;
			void		write(const void* data, size_t size) override { text.append(static_cast<const char*>(data), size); }
		};

	} // namespace

	std::optional<float> parseSvgNumber(const char* text) {
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

	LodSvg readLodSvg(const LodSvgArgs& args, std::vector<std::string>& issues) {
		LodSvg out;

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
		out.parsed = true;
		if (const pugi::xml_attribute viewBox = root.attribute("viewBox")) {
			out.viewBoxText = viewBox.value();
			out.viewBox = parseViewBox(viewBox.value());
		}
		// NanoSVG scales the viewBox to the width and height; without them it keeps viewBox units.
		root.remove_attribute("width");
		root.remove_attribute("height");

		Walk walk(args, issues);
		walk.visit(root, 0.0F, false);
		out.elements = std::move(walk.elements);

		TextWriter written;
		doc.save(written, "", pugi::format_raw | pugi::format_no_declaration);
		std::vector<renderer::LoadedSVGShape> shapes;
		if (!renderer::loadSVG(std::as_bytes(std::span(written.text)), args.curveTolerance, shapes, args.name)) {
			issues.emplace_back("draws no shapes");
			return out;
		}
		for (renderer::LoadedSVGShape& shape : shapes) {
			const std::string& id = shape.id;
			size_t			   index = 0;
			const auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), index);
			if (error != std::errc{} || end != id.data() + id.size() || index >= out.elements.size()) {
				issues.push_back("a shape the reader can't trace to its element (id \"" + id + "\")");
				continue;
			}
			out.elements[index].drawn = true;
			out.shapes.push_back({.shape = std::move(shape), .element = index});
		}
		return out;
	}

} // namespace assets
