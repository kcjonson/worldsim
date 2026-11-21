#include "assets/PartsAsset.h"

#include "assets/LodSvg.h"
#include "assets/PointTurn.h"
#include "assets/UseAreaRoom.h"

#include <boolean/RingBoolean.h>
#include <polygon/Polygon.h>
#include <vector/ArtBuild.h>

#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <utility>

namespace assets {

	namespace {

		using Ring2 = std::vector<glm::vec2>;

		// The geometry library's unit: its booleans simplify by a millimetre and the nav mesh admits rings
		// rounded to the millimetre, so the union and the overlap test round there too.
		constexpr double kMillimetresPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);
		constexpr float	 kPi = std::numbers::pi_v<float>;

		std::string formatted(const char* format, auto... values) {
			std::array<char, 256> buffer{};
			std::snprintf(buffer.data(), buffer.size(), format, values...);
			return buffer.data();
		}

		std::string inQuotes(const std::string& text) {
			return "'" + text + "'";
		}

		// ─── Reading the XML ─────────────────────────────────────────────────────────────────────────

		class XmlReader {
		  public:
			XmlReader(const std::string& newDefName, ValidationReport& newReport)
				: defName(newDefName),
				  report(newReport) {}

			/// An attribute as a finite number; `fallback` when it is missing (an error when there is none).
			float number(const pugi::xml_node& node, const char* attribute, const std::string& where, std::optional<float> fallback = std::nullopt) {
				const pugi::xml_attribute found = node.attribute(attribute);
				if (!found) {
					if (!fallback.has_value()) {
						error(where, std::string("<") + node.name() + "> needs " + attribute);
					}
					return fallback.value_or(0.0F);
				}
				const std::optional<float> value = parseSvgNumber(found.value());
				if (!value.has_value()) {
					error(where, std::string("<") + node.name() + "> " + attribute + "=\"" + found.value() + "\" is not a number");
					return fallback.value_or(0.0F);
				}
				return *value;
			}

			void unknown(const pugi::xml_node& node, const std::string& where) {
				error(where, std::string("<") + node.name() + "> is not read here");
			}

			void error(const std::string& where, const std::string& message) {
				report.add(Severity::Error, defName, "parts", message, where);
			}

		  private:
			const std::string& defName;
			ValidationReport&  report;
		};

		std::vector<std::string> words(const std::string& text) {
			std::istringstream		 stream(text);
			std::vector<std::string> out;
			for (std::string word; stream >> word;) {
				out.push_back(word);
			}
			return out;
		}

		void readFootprint(const pugi::xml_node& node, AuthoredFootprint& footprint, const std::string& where, XmlReader& reader) {
			for (const pugi::xml_node shape : node.children()) {
				if (shape.type() != pugi::node_element) {
					continue;
				}
				const std::string_view name = shape.name();
				if (name == "rect") {
					footprint.shape = AuthoredFootprint::Shape::Rect;
					footprint.minX = reader.number(shape, "minX", where);
					footprint.minY = reader.number(shape, "minY", where);
					footprint.maxX = reader.number(shape, "maxX", where);
					footprint.maxY = reader.number(shape, "maxY", where);
				} else if (name == "round") {
					footprint.shape = AuthoredFootprint::Shape::Round;
					footprint.x = reader.number(shape, "x", where, 0.0F);
					footprint.y = reader.number(shape, "y", where, 0.0F);
					footprint.radius = reader.number(shape, "radius", where);
					footprint.sides = reader.number(shape, "sides", where);
				} else {
					reader.unknown(shape, where);
					continue;
				}
				++footprint.shapeCount;
			}
		}

		void readAreaShapes(const pugi::xml_node& node, AuthoredArea& area, const std::string& where, XmlReader& reader) {
			for (const pugi::xml_node shape : node.children()) {
				if (shape.type() != pugi::node_element) {
					continue;
				}
				const std::string_view name = shape.name();
				AuthoredAreaShape	   out;
				if (name == "around") {
					out.kind = AuthoredAreaShape::Kind::Around;
					out.distanceMeters = reader.number(shape, "distanceMeters", where);
				} else if (name == "circle") {
					out.kind = AuthoredAreaShape::Kind::Circle;
					out.x = reader.number(shape, "x", where, 0.0F);
					out.y = reader.number(shape, "y", where, 0.0F);
					out.radius = reader.number(shape, "radius", where);
				} else if (name == "rect" || name == "drawing") {
					out.kind = name == "rect" ? AuthoredAreaShape::Kind::Rect : AuthoredAreaShape::Kind::Drawing;
					out.minX = reader.number(shape, "minX", where);
					out.minY = reader.number(shape, "minY", where);
					out.maxX = reader.number(shape, "maxX", where);
					out.maxY = reader.number(shape, "maxY", where);
					out.file = shape.attribute("file").as_string();
					if (out.kind == AuthoredAreaShape::Kind::Drawing && out.file.empty()) {
						reader.error(where, "<drawing> needs a file");
					}
				} else if (name == "area") {
					continue; // read by the caller
				} else {
					reader.unknown(shape, where);
					continue;
				}
				area.shapes.push_back(std::move(out));
			}
		}

		// ─── Geometry helpers ────────────────────────────────────────────────────────────────────────

		geometry::Ring toMillimetres(const Ring2& ring) {
			geometry::Ring out;
			out.reserve(ring.size());
			for (const glm::vec2& point : ring) {
				out.push_back(
					{std::llround(static_cast<double>(point.x) * kMillimetresPerMeter), std::llround(static_cast<double>(point.y) * kMillimetresPerMeter)}
				);
			}
			return out;
		}

		Ring2 toMeters(const geometry::Ring& ring) {
			Ring2 out;
			out.reserve(ring.size());
			for (const geometry::Vec2i64& point : ring) {
				out.push_back({static_cast<float>(static_cast<double>(point.x) / kMillimetresPerMeter),
							   static_cast<float>(static_cast<double>(point.y) / kMillimetresPerMeter)});
			}
			return out;
		}

		/// How deep two parts must reach into each other to overlap in plan: parts authored to touch, flush at any
		/// turn, differ by float error far under a millimetre, and an overlap a millimetre deep is one on purpose.
		constexpr double kOverlapToleranceMeters = 0.001;

		/// Whether two convex footprints (a rect or a regular polygon, wound with interior on the left) overlap in
		/// plan: shrunk inward by half the tolerance each, they still share area. Each footprint is the
		/// intersection of its edges' half-planes, so the shrunk overlap is the first ring clipped by every half-
		/// plane of both, each moved inward by half the tolerance. Unrounded, in double.
		bool overlapInPlan(const Ring2& first, const Ring2& second) {
			std::vector<glm::dvec2> clipped;
			for (const glm::vec2& point : first) {
				clipped.emplace_back(point);
			}
			const double inset = kOverlapToleranceMeters * 0.5;
			for (const Ring2* ring : {&first, &second}) {
				for (size_t i = 0; i < ring->size() && clipped.size() >= 3; ++i) {
					const glm::dvec2 start((*ring)[i]);
					const glm::dvec2 end((*ring)[(i + 1) % ring->size()]);
					const glm::dvec2 along = end - start;
					const double	 length = std::hypot(along.x, along.y);
					// Signed distance inside the edge's half-plane, less the inset: kept where it is 0 or more.
					const auto inside = [&](const glm::dvec2& point) {
						return (along.x * (point.y - start.y) - along.y * (point.x - start.x)) / length - inset;
					};
					std::vector<glm::dvec2> kept;
					for (size_t j = 0; j < clipped.size(); ++j) {
						const glm::dvec2& current = clipped[j];
						const glm::dvec2& next = clipped[(j + 1) % clipped.size()];
						const double	  currentSide = inside(current);
						const double	  nextSide = inside(next);
						if (currentSide >= 0.0) {
							kept.push_back(current);
						}
						if ((currentSide >= 0.0) != (nextSide >= 0.0)) {
							kept.push_back(current + (next - current) * (currentSide / (currentSide - nextSide)));
						}
					}
					clipped = std::move(kept);
				}
			}
			double doubledArea = 0.0;
			for (size_t j = 0; j < clipped.size(); ++j) {
				const glm::dvec2& current = clipped[j];
				const glm::dvec2& next = clipped[(j + 1) % clipped.size()];
				doubledArea += current.x * next.y - next.x * current.y;
			}
			return clipped.size() >= 3 && doubledArea > 1.0e-12;
		}

		/// The union of groups of rings in metres, each group filled by the nonzero rule.
		std::vector<AreaPolygon> unionInMeters(const std::vector<std::vector<Ring2>>& groups) {
			std::vector<std::vector<geometry::Ring>> converted;
			converted.reserve(groups.size());
			for (const std::vector<Ring2>& group : groups) {
				std::vector<geometry::Ring>& out = converted.emplace_back();
				for (const Ring2& ring : group) {
					out.push_back(toMillimetres(ring));
				}
			}
			std::vector<AreaPolygon> region;
			for (const geometry::RegionPolygon& polygon : geometry::unionOfFills(converted)) {
				AreaPolygon& out = region.emplace_back();
				out.outer = toMeters(polygon.outer);
				for (const geometry::Ring& hole : polygon.holes) {
					out.holes.push_back(toMeters(hole));
				}
			}
			return region;
		}

		double ringArea(const Ring2& ring) {
			double doubled = 0.0;
			for (size_t i = 0; i < ring.size(); ++i) {
				const glm::vec2& from = ring[i];
				const glm::vec2& to = ring[(i + 1) % ring.size()];
				doubled += static_cast<double>(from.x) * to.y - static_cast<double>(to.x) * from.y;
			}
			return doubled * 0.5;
		}

		double regionArea(const std::vector<AreaPolygon>& region) {
			double area = 0.0;
			for (const AreaPolygon& polygon : region) {
				area += ringArea(polygon.outer);
				for (const Ring2& hole : polygon.holes) {
					area += ringArea(hole);
				}
			}
			return area;
		}

		/// A regular polygon on a circle, its edges within `tolerance` of it.
		Ring2 disc(glm::vec2 center, float radius, float tolerance) {
			const float ratio = std::clamp(1.0F - tolerance / radius, -1.0F, 1.0F);
			const int	sides = std::max(8, static_cast<int>(std::ceil(kPi / std::acos(ratio))));
			Ring2		ring;
			ring.reserve(static_cast<size_t>(sides));
			for (int i = 0; i < sides; ++i) {
				const float angle = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(sides);
				ring.push_back(center + radius * glm::vec2{std::cos(angle), std::sin(angle)});
			}
			return ring;
		}

		float crossOf(glm::vec2 origin, glm::vec2 first, glm::vec2 second) {
			return (first.x - origin.x) * (second.y - origin.y) - (first.y - origin.y) * (second.x - origin.x);
		}

	} // namespace

	std::vector<glm::vec2> convexHull(std::vector<glm::vec2> points) {
		std::sort(points.begin(), points.end(), [](glm::vec2 left, glm::vec2 right) { return left.x != right.x ? left.x < right.x : left.y < right.y; });
		points.erase(std::unique(points.begin(), points.end()), points.end());
		if (points.size() < 3) {
			return points;
		}
		std::vector<glm::vec2> hull(points.size() * 2);
		size_t				   count = 0;
		for (const glm::vec2& point : points) {
			while (count >= 2 && crossOf(hull[count - 2], hull[count - 1], point) <= 0.0F) {
				--count;
			}
			hull[count++] = point;
		}
		const size_t lower = count + 1;
		for (size_t i = points.size() - 1; i-- > 0;) {
			while (count >= lower && crossOf(hull[count - 2], hull[count - 1], points[i]) <= 0.0F) {
				--count;
			}
			hull[count++] = points[i];
		}
		hull.resize(count - 1);
		return hull;
	}

	namespace {

		bool isConvex(const Ring2& ring) {
			for (size_t i = 0; i < ring.size(); ++i) {
				if (crossOf(ring[i], ring[(i + 1) % ring.size()], ring[(i + 2) % ring.size()]) < 0.0F) {
					return false;
				}
			}
			return true;
		}

		Ring2 minkowski(const Ring2& points, const Ring2& brush) {
			Ring2 sums;
			sums.reserve(points.size() * brush.size());
			for (const glm::vec2& point : points) {
				for (const glm::vec2& offset : brush) {
					sums.push_back(point + offset);
				}
			}
			return convexHull(std::move(sums));
		}

		/// Rings grown outward by `distance` (corners rounded to `tolerance`), as groups for unionInMeters: a convex
		/// ring is its hull with a disc, a concave one itself and one hull a side, the side swept by the disc.
		void appendGrown(const std::vector<Ring2>& rings, float distance, float tolerance, std::vector<std::vector<Ring2>>& groups) {
			if (distance <= 0.0F) {
				groups.push_back(rings);
				return;
			}
			const Ring2 brush = disc({0.0F, 0.0F}, distance, tolerance);
			for (const Ring2& ring : rings) {
				if (isConvex(ring)) {
					groups.push_back({minkowski(ring, brush)});
					continue;
				}
				groups.push_back({ring});
				for (size_t i = 0; i < ring.size(); ++i) {
					groups.push_back({minkowski({ring[i], ring[(i + 1) % ring.size()]}, brush)});
				}
			}
		}

	} // namespace

	std::pair<float, float> ySpanAt(const std::vector<glm::vec2>& ring, float x) {
		float low = std::numeric_limits<float>::max();
		float high = std::numeric_limits<float>::lowest();
		for (size_t i = 0; i < ring.size(); ++i) {
			const glm::vec2& from = ring[i];
			const glm::vec2& to = ring[(i + 1) % ring.size()];
			if ((from.x - x) * (to.x - x) > 0.0F) {
				continue;
			}
			const bool	vertical = from.x == to.x;
			const float y = vertical ? std::min(from.y, to.y) : from.y + (to.y - from.y) * (x - from.x) / (to.x - from.x);
			const float other = vertical ? std::max(from.y, to.y) : y;
			low = std::min(low, y);
			high = std::max(high, other);
		}
		return {low, high};
	}

	namespace {

		// ─── Drawings ────────────────────────────────────────────────────────────────────────────────

		/// One place a drawing is drawn: what it covers and how the error names it. `target` receives the mapped
		/// drawing's index (unset for an area's drawing, which is read for its fills).
		struct DrawingUse {
			std::string				 file;
			glm::vec2				 coversMeters{0.0F};
			std::string				 what;
			std::optional<uint32_t>* target = nullptr;
			std::vector<std::vector<Ring2>>* fillGroups = nullptr; ///< an area drawing's filled shapes go here, a nonzero group each
			glm::vec2				 originMeters{0.0F}; ///< where an area drawing's top left lands in the item's frame
			bool					 failed = false;
		};

		std::string withoutSpaces(std::string_view text) {
			std::string out;
			for (const char character : text) {
				if (std::isspace(static_cast<unsigned char>(character)) == 0) {
					out.push_back(character);
				}
			}
			return out;
		}

		/// Whether an element sets fill-rule to evenodd, as an attribute or in its inline style.
		bool fillsEvenOdd(const pugi::xml_node& node) {
			if (withoutSpaces(node.attribute("fill-rule").value()) == "evenodd") {
				return true;
			}
			return withoutSpaces(node.attribute("style").value()).find("fill-rule:evenodd") != std::string::npos;
		}

		std::optional<std::array<float, 4>> viewBoxOf(const std::string& bytes) {
			pugi::xml_document doc;
			if (!doc.load_buffer(bytes.data(), bytes.size())) {
				return std::nullopt;
			}
			const pugi::xml_node root = doc.document_element();
			std::array<float, 4> box{};
			std::istringstream	 stream(std::string(root.attribute("viewBox").value()));
			std::string			 text;
			for (float& value : box) {
				if (!(stream >> text)) {
					return std::nullopt;
				}
				text.erase(std::remove(text.begin(), text.end(), ','), text.end());
				const std::optional<float> number = parseSvgNumber(text.c_str());
				if (!number.has_value()) {
					return std::nullopt;
				}
				value = *number;
			}
			return box;
		}

		/// The shape moved from viewBox units onto [0, size] metres: vertices scaled, stroke width by the mean
		/// scale, and the gradient's transform composed with the inverse, so each vertex keeps its colour.
		renderer::LoadedSVGShape mapped(renderer::LoadedSVGShape shape, glm::vec2 origin, glm::vec2 scale, glm::vec2 sizeMeters) {
			for (renderer::VectorPath& path : shape.paths) {
				for (Foundation::Vec2& vertex : path.vertices) {
					vertex.x = (vertex.x - origin.x) * scale.x;
					vertex.y = (vertex.y - origin.y) * scale.y;
				}
			}
			shape.strokeWidth *= std::sqrt(scale.x * scale.y);
			float(&xform)[6] = shape.gradient.xform;
			const float a = xform[0];
			const float b = xform[1];
			const float c = xform[2];
			const float d = xform[3];
			xform[4] += a * origin.x + c * origin.y;
			xform[5] += b * origin.x + d * origin.y;
			xform[0] = a / scale.x;
			xform[1] = b / scale.x;
			xform[2] = c / scale.y;
			xform[3] = d / scale.y;
			shape.width = sizeMeters.x;
			shape.height = sizeMeters.y;
			return shape;
		}

		/// What a drawing shows, averaged: its shapes painted in order onto a 32 x 32 lattice of points over its size (a
		/// later shape over an earlier one, each by its alpha, as SVG paints them), then the painted points' colours
		/// averaged by their coverage. Opaque. Detail finer than a lattice cell may fall between the points.
		Foundation::Color averageColorOf(const std::vector<ItemDrawingShape>& shapes, glm::vec2 size) {
			constexpr int					  kLattice = 32;
			std::array<glm::dvec4, kLattice * kLattice> painted{}; // premultiplied colour and coverage
			const glm::vec2					  cell = size / static_cast<float>(kLattice);
			for (const ItemDrawingShape& item : shapes) {
				renderer::TessellatedMesh mesh;
				renderer::appendShapeMesh(item.shape, mesh);
				for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
					const std::array<uint16_t, 3> corners = {mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]};
					const glm::vec2				  first = mesh.vertices[corners[0]];
					const glm::vec2				  second = mesh.vertices[corners[1]];
					const glm::vec2				  third = mesh.vertices[corners[2]];
					const float					  doubled = crossOf(first, second, third);
					if (doubled == 0.0F) {
						continue;
					}
					// A lattice point on an edge two triangles share is painted by one of them (the top-left rule): an edge
					// owns its points when, taken with the triangle wound positively, it runs down or exactly leftward. The
					// side is computed from the edge's ends in one fixed order, so the two triangles get exactly opposite
					// values and no point falls between them.
					const float sign = doubled > 0.0F ? 1.0F : -1.0F;
					const auto	covers = [sign](glm::vec2 from, glm::vec2 to, glm::vec2 point) {
						 const bool	 ordered = from.x < to.x || (from.x == to.x && from.y < to.y);
						 const float side = sign * (ordered ? crossOf(from, to, point) : -crossOf(to, from, point));
						 if (side != 0.0F) {
							 return side > 0.0F;
						 }
						 const glm::vec2 along = (to - from) * sign;
						 return along.y > 0.0F || (along.y == 0.0F && along.x < 0.0F);
					};
					const glm::vec2 low = glm::min(first, glm::min(second, third));
					const glm::vec2 high = glm::max(first, glm::max(second, third));
					const int		fromColumn = std::max(0, static_cast<int>(std::ceil(low.x / cell.x - 0.5F)));
					const int		toColumn = std::min(kLattice - 1, static_cast<int>(std::floor(high.x / cell.x - 0.5F)));
					const int		fromRow = std::max(0, static_cast<int>(std::ceil(low.y / cell.y - 0.5F)));
					const int		toRow = std::min(kLattice - 1, static_cast<int>(std::floor(high.y / cell.y - 0.5F)));
					for (int row = fromRow; row <= toRow; ++row) {
						for (int column = fromColumn; column <= toColumn; ++column) {
							const glm::vec2 point{(static_cast<float>(column) + 0.5F) * cell.x, (static_cast<float>(row) + 0.5F) * cell.y};
							if (!covers(second, third, point) || !covers(third, first, point) || !covers(first, second, point)) {
								continue;
							}
							const float weightFirst = crossOf(point, second, third) / doubled;
							const float weightSecond = crossOf(first, point, third) / doubled;
							const float weightThird = 1.0F - weightFirst - weightSecond;
							glm::dvec4 color{0.0};
							for (size_t corner = 0; corner < 3; ++corner) {
								const Foundation::Color& vertex = mesh.colors[corners[corner]];
								const double			 weight = corner == 0 ? weightFirst : corner == 1 ? weightSecond : weightThird;
								color += weight * glm::dvec4(vertex.r, vertex.g, vertex.b, vertex.a);
							}
							glm::dvec4& under = painted[static_cast<size_t>(row * kLattice + column)];
							under = under * (1.0 - color.a) + glm::dvec4(glm::dvec3(color) * color.a, color.a);
						}
					}
				}
			}
			glm::dvec4 total{0.0};
			for (const glm::dvec4& point : painted) {
				total += point;
			}
			if (total.a <= 0.0) {
				return {0.0F, 0.0F, 0.0F, 1.0F};
			}
			return {static_cast<float>(total.r / total.a), static_cast<float>(total.g / total.a), static_cast<float>(total.b / total.a), 1.0F};
		}

		bool sameAspect(glm::vec2 drawing, glm::vec2 covered, float tolerance) {
			return std::abs((drawing.x / drawing.y) / (covered.x / covered.y) - 1.0F) <= tolerance;
		}

		std::string describeAspect(glm::vec2 size) {
			return formatted("%g x %g (%.3f)", static_cast<double>(size.x), static_cast<double>(size.y), static_cast<double>(size.x / size.y));
		}

		class Resolver {
		  public:
			Resolver(const std::filesystem::path& newFolder, const std::string& newDefName, const PartsLoadConfig& newConfig, ValidationReport& newReport)
				: folder(newFolder),
				  defName(newDefName),
				  config(newConfig),
				  report(newReport) {}

			void error(const std::string& field, const std::string& message, const std::string& context = "") {
				report.add(Severity::Error, defName, field, message, context);
			}

			/// Reads every used drawing once and maps it onto each size it covers.
			void loadDrawings(std::vector<DrawingUse>& uses, std::vector<ItemDrawing>& drawings) {
				std::map<std::string, std::vector<DrawingUse*>> byFile;
				for (DrawingUse& use : uses) {
					byFile[use.file].push_back(&use);
				}
				for (auto& [file, fileUses] : byFile) {
					const auto fail = [&](const std::string& message, const std::string& context) {
						error("drawing", message, context);
						for (DrawingUse* use : fileUses) {
							use->failed = true;
						}
					};
					const std::filesystem::path relative(file);
					const std::filesystem::path resolved = (folder / relative).lexically_normal();
					const std::filesystem::path back = resolved.lexically_relative(folder.lexically_normal());
					if (file.rfind("@shared/", 0) == 0 || relative.is_absolute() || relative.has_root_name() || back.empty() ||
						*back.begin() == "..") {
						fail("drawing " + inQuotes(file) + " is outside the item's folder; parts and their art belong to their item", fileUses.front()->what);
						continue;
					}
					std::ifstream in(resolved, std::ios::binary);
					if (!in) {
						fail("drawing " + inQuotes(file) + " does not exist", resolved.string());
						continue;
					}
					const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
					const std::optional<std::array<float, 4>> box = viewBoxOf(bytes);
					if (!box.has_value() || (*box)[2] <= 0.0F || (*box)[3] <= 0.0F) {
						fail("drawing " + inQuotes(file) + " has no viewBox with a width and height; its proportions are the viewBox's", resolved.string());
						continue;
					}
					const glm::vec2 viewBoxSize{(*box)[2], (*box)[3]};
					float			finestScale = 0.0F;
					for (const DrawingUse* use : fileUses) {
						finestScale = std::max({finestScale, use->coversMeters.x / viewBoxSize.x, use->coversMeters.y / viewBoxSize.y});
					}
					// Rule 6: the engine's art check, what loadSVG would drop or draw as plain geometry.
					std::vector<renderer::svg::Problem> problems;
					renderer::svg::checkArt(file, bytes, problems);
					for (const renderer::svg::Problem& problem : problems) {
						error("drawing", "drawing " + problem.text(), resolved.string());
					}

					// An area drawing's fills are unioned by the nonzero rule, so an even-odd fill would read as another shape.
					const bool areaDrawing =
						std::any_of(fileUses.begin(), fileUses.end(), [](const DrawingUse* use) { return use->fillGroups != nullptr; });
					std::vector<std::string> issues;
					const auto				 checkElement = [&](const pugi::xml_node& node, const std::string& where, bool, std::optional<size_t>) {
						  if (areaDrawing && fillsEvenOdd(node)) {
							  issues.push_back(where + " fills by fill-rule evenodd; an area drawing fills by the nonzero rule, so wind a hole against its outline");
						  }
					};
					const LodSvg svg = readLodSvg(
						{.svg = std::as_bytes(std::span(bytes)),
						 .curveTolerance = finestScale > 0.0F ? config.curveToleranceMeters / finestScale : 1.0F,
						 .name = resolved.string(),
						 .onElement = checkElement},
						issues
					);
					const bool artSaysNothing = std::any_of(problems.begin(), problems.end(), [](const renderer::svg::Problem& problem) {
						return problem.message.starts_with("draws nothing");
					});
					for (const std::string& issue : issues) {
						// The art check reports a drawing that draws nothing already.
						if (issue == "draws no shapes" && artSaysNothing) {
							continue;
						}
						error("drawing", "drawing " + inQuotes(file) + ": " + issue, resolved.string());
					}

					const glm::vec2				   origin{(*box)[0], (*box)[1]};
					std::map<std::pair<long, long>, uint32_t> mappedBySize;
					bool						   proportionReported = false;
					for (DrawingUse* use : fileUses) {
						if (!sameAspect(viewBoxSize, use->coversMeters, config.aspectToleranceRelative)) {
							use->failed = true;
							if (!proportionReported) {
								error(
									"drawing",
									"drawing " + inQuotes(file) + "'s viewBox is " + describeAspect(viewBoxSize) + ", but " + use->what + " is " +
										describeAspect(use->coversMeters) + "; their proportions must match",
									resolved.string()
								);
								proportionReported = true;
							}
							continue;
						}
						const glm::vec2 scale = use->coversMeters / viewBoxSize;
						if (use->fillGroups != nullptr) {
							size_t filled = 0;
							for (const LodSvgShape& loaded : svg.shapes) {
								if (!loaded.shape.hasFill) {
									continue;
								}
								std::vector<Ring2>& group = use->fillGroups->emplace_back();
								for (const renderer::VectorPath& path : mapped(loaded.shape, origin, scale, use->coversMeters).paths) {
									Ring2& ring = group.emplace_back();
									for (const Foundation::Vec2& vertex : path.vertices) {
										ring.push_back(use->originMeters + glm::vec2{vertex.x, vertex.y});
									}
								}
								++filled;
							}
							if (filled == 0) {
								use->failed = true;
								error("useAreas", "area drawing " + inQuotes(file) + " has no filled shape", use->what);
							}
							continue;
						}
						// Sizes equal to a micrometre are one size: a round's faces differ in their last float bits.
						const auto key = std::make_pair(std::lround(use->coversMeters.x * 1.0e6F), std::lround(use->coversMeters.y * 1.0e6F));
						auto	   found = mappedBySize.find(key);
						if (found == mappedBySize.end()) {
							ItemDrawing drawing{.file = file, .sizeMeters = use->coversMeters, .shapes = {}};
							for (const LodSvgShape& loaded : svg.shapes) {
								drawing.shapes.push_back(
									{.shape = mapped(loaded.shape, origin, scale, use->coversMeters),
									 .minPixelsPerMeter = svg.elements[loaded.element].minPixelsPerMeter}
								);
							}
							drawing.averageColor = averageColorOf(drawing.shapes, drawing.sizeMeters);
							checkOverflow(drawing, use->what);
							found = mappedBySize.emplace(key, static_cast<uint32_t>(drawings.size())).first;
							drawings.push_back(std::move(drawing));
						}
						*use->target = found->second;
					}
				}
			}

		  private:
			/// Rule 8: art past the viewBox by more than overflowMeters is lost to the face's clip; a warning.
			void checkOverflow(const ItemDrawing& drawing, const std::string& what) {
				const float limit = config.overflowMeters;
				for (const ItemDrawingShape& item : drawing.shapes) {
					const float halfStroke = item.shape.hasStroke ? item.shape.strokeWidth * 0.5F : 0.0F;
					for (const renderer::VectorPath& path : item.shape.paths) {
						for (const Foundation::Vec2& vertex : path.vertices) {
							if (vertex.x - halfStroke < -limit || vertex.y - halfStroke < -limit ||
								vertex.x + halfStroke > drawing.sizeMeters.x + limit || vertex.y + halfStroke > drawing.sizeMeters.y + limit) {
								report.add(
									Severity::Warning,
									defName,
									"drawing",
									"drawing " + inQuotes(drawing.file) + " draws past its viewBox by more than " +
										formatted("%g", static_cast<double>(limit)) + " m on " + what + "; art past the edge is clipped away",
									(folder / drawing.file).string()
								);
								return;
							}
						}
					}
				}
			}

			const std::filesystem::path& folder;
			const std::string&			 defName;
			const PartsLoadConfig&		 config;
			ValidationReport&			 report;
		};

		constexpr std::array<const char*, 4> kRectFaceNames = {"back", "right", "front", "left"};

		/// Whether a valid footprint is too thin for rule 9 to see: shrunk inward by half the overlap tolerance it
		/// would have no area, so nothing could ever overlap it.
		bool thinnerThanOverlapTolerance(const AuthoredFootprint& footprint) {
			if (footprint.shape == AuthoredFootprint::Shape::Rect) {
				return static_cast<double>(footprint.maxX - footprint.minX) < kOverlapToleranceMeters ||
					   static_cast<double>(footprint.maxY - footprint.minY) < kOverlapToleranceMeters;
			}
			const double inradius = static_cast<double>(footprint.radius) * std::cos(std::numbers::pi / static_cast<double>(footprint.sides));
			return inradius < kOverlapToleranceMeters * 0.5;
		}

		/// A footprint's polygon in the part's frame, or empty when rule 3 refuses it.
		Ring2 footprintPolygon(const AuthoredFootprint& footprint) {
			if (footprint.shapeCount != 1) {
				return {};
			}
			if (footprint.shape == AuthoredFootprint::Shape::Rect) {
				if (!(footprint.maxX > footprint.minX && footprint.maxY > footprint.minY)) {
					return {};
				}
				return {{footprint.minX, footprint.minY}, {footprint.maxX, footprint.minY}, {footprint.maxX, footprint.maxY}, {footprint.minX, footprint.maxY}};
			}
			if (!(footprint.radius > 0.0F) || footprint.sides != std::floor(footprint.sides) || footprint.sides < 3.0F || footprint.sides > 32.0F) {
				return {};
			}
			const auto sides = static_cast<int32_t>(footprint.sides);
			Ring2	   ring;
			for (int32_t i = 0; i < sides; ++i) {
				const float angle = (static_cast<float>(i) + 0.5F) * 2.0F * kPi / static_cast<float>(sides);
				ring.push_back({footprint.x + footprint.radius * std::cos(angle), footprint.y + footprint.radius * std::sin(angle)});
			}
			return ring;
		}

		std::vector<std::string> cycleIn(const std::vector<std::vector<size_t>>& later, const std::vector<std::string>& names) {
			// Depth-first search for a back edge; the stack from its target is the cycle.
			std::vector<int>	state(later.size(), 0);
			std::vector<size_t> stack;
			std::vector<std::string> found;
			const auto visit = [&](auto&& self, size_t node) -> bool {
				state[node] = 1;
				stack.push_back(node);
				for (const size_t next : later[node]) {
					if (state[next] == 1) {
						for (auto member = std::find(stack.begin(), stack.end(), next); member != stack.end(); ++member) {
							found.push_back(names[*member]);
						}
						return true;
					}
					if (state[next] == 0 && self(self, next)) {
						return true;
					}
				}
				stack.pop_back();
				state[node] = 2;
				return false;
			};
			for (size_t node = 0; node < later.size() && found.empty(); ++node) {
				if (state[node] == 0) {
					visit(visit, node);
				}
			}
			std::sort(found.begin(), found.end());
			found.erase(std::unique(found.begin(), found.end()), found.end());
			return found;
		}

	} // namespace

	std::optional<Foundation::Color> parseRgbHexColor(std::string_view text) {
		if (text.size() != 7 || text[0] != '#') {
			return std::nullopt;
		}
		std::array<float, 3> channels{};
		for (size_t i = 0; i < 3; ++i) {
			unsigned value = 0;
			for (const char digit : text.substr(1 + i * 2, 2)) {
				const int nibble = std::isdigit(static_cast<unsigned char>(digit)) != 0 ? digit - '0'
								   : (digit >= 'a' && digit <= 'f')				   ? digit - 'a' + 10
								   : (digit >= 'A' && digit <= 'F')				   ? digit - 'A' + 10
																				   : -1;
				if (nibble < 0) {
					return std::nullopt;
				}
				value = value * 16 + static_cast<unsigned>(nibble);
			}
			channels[i] = static_cast<float>(value) / 255.0F;
		}
		return Foundation::Color{channels[0], channels[1], channels[2], 1.0F};
	}

	std::vector<glm::vec2> turnedFootprint(const PartsAsset& asset, size_t placement, float itemTurnRadians) {
		std::vector<glm::vec2> ring;
		ring.reserve(asset.placedFootprints[placement].size());
		for (const glm::vec2& point : asset.placedFootprints[placement]) {
			ring.push_back(turnedPoint(point, itemTurnRadians));
		}
		return ring;
	}

	std::vector<AreaPolygon> reservedRegion(const AssetDefinition& def) {
		std::vector<std::vector<Ring2>> groups;
		for (const CollisionShape& collider : def.partsColliders) {
			groups.push_back({collider.pointsMeters});
		}
		if (def.parts != nullptr) {
			for (const AreaPolygon& polygon : def.parts->clearance.polygons) {
				std::vector<Ring2>& group = groups.emplace_back();
				group.push_back(polygon.outer);
				group.insert(group.end(), polygon.holes.begin(), polygon.holes.end());
			}
		}
		return groups.empty() ? std::vector<AreaPolygon>{} : unionInMeters(groups);
	}

	std::vector<std::vector<size_t>> partsDrawnAfter(const PartsAsset& asset, const std::vector<std::vector<glm::vec2>>& screenFootprints) {
		const size_t					 count = screenFootprints.size();
		std::vector<std::vector<size_t>> later(count);
		const auto xRange = [](const Ring2& ring) {
			const auto [low, high] = std::minmax_element(ring.begin(), ring.end(), [](glm::vec2 left, glm::vec2 right) { return left.x < right.x; });
			return std::make_pair(low->x, high->x);
		};
		for (size_t i = 0; i < count; ++i) {
			for (size_t j = i + 1; j < count; ++j) {
				if (screenFootprints[i].empty() || screenFootprints[j].empty()) {
					continue;
				}
				const int8_t order = asset.planOrder[i * count + j];
				if (asset.overlapsInPlan(i, j)) {
					if (order > 0) {
						later[j].push_back(i);
					} else if (order < 0) {
						later[i].push_back(j);
					}
					continue;
				}
				const auto [firstLow, firstHigh] = xRange(screenFootprints[i]);
				const auto [secondLow, secondHigh] = xRange(screenFootprints[j]);
				const float low = std::max(firstLow, secondLow);
				const float high = std::min(firstHigh, secondHigh);
				if (high <= low) {
					continue;
				}
				// Disjoint and convex, so which is further south is the same on every shared vertical line.
				const auto [firstTop, firstBottom] = ySpanAt(screenFootprints[i], (low + high) * 0.5F);
				const auto [secondTop, secondBottom] = ySpanAt(screenFootprints[j], (low + high) * 0.5F);
				if (firstTop + firstBottom > secondTop + secondBottom) {
					later[j].push_back(i);
				} else {
					later[i].push_back(j);
				}
			}
		}
		return later;
	}

	std::vector<uint32_t> partsDrawOrder(const PartsAsset& asset, float itemTurnRadians) {
		const size_t					 count = asset.placements.size();
		std::vector<std::vector<glm::vec2>> screen(count);
		for (size_t i = 0; i < count; ++i) {
			screen[i] = turnedFootprint(asset, i, itemTurnRadians);
		}
		const std::vector<std::vector<size_t>> later = partsDrawnAfter(asset, screen);
		std::vector<uint32_t>				   waiting(count, 0);
		for (const std::vector<size_t>& after : later) {
			for (const size_t placement : after) {
				++waiting[placement];
			}
		}
		// Kahn's sort, the earliest placement first among those ready, so ties keep placement order.
		std::vector<uint32_t> order;
		std::vector<bool>	  done(count, false);
		order.reserve(count);
		while (order.size() < count) {
			size_t next = count;
			for (size_t i = 0; i < count; ++i) {
				if (!done[i] && waiting[i] == 0) {
					next = i;
					break;
				}
			}
			if (next == count) {
				// A cycle, which validation reports (rule 9): the rest in placement order.
				for (size_t i = 0; i < count; ++i) {
					if (!done[i]) {
						done[i] = true;
						order.push_back(static_cast<uint32_t>(i));
					}
				}
				break;
			}
			done[next] = true;
			order.push_back(static_cast<uint32_t>(next));
			for (const size_t placement : later[next]) {
				--waiting[placement];
			}
		}
		return order;
	}

	PartsAuthored readPartsXml(const pugi::xml_node& defNode, const std::string& defName, ValidationReport& report) {
		PartsAuthored authored;
		XmlReader	  reader(defName, report);

		for (const pugi::xml_node partNode : defNode.child("parts").children()) {
			if (partNode.type() != pugi::node_element) {
				continue;
			}
			if (std::string_view(partNode.name()) != "part") {
				reader.unknown(partNode, "<parts>");
				continue;
			}
			AuthoredPart part;
			part.name = partNode.attribute("name").as_string();
			const std::string where = "part " + inQuotes(part.name);
			for (const pugi::xml_node child : partNode.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view name = child.name();
				if (name == "footprint") {
					readFootprint(child, part.footprint, where, reader);
				} else if (name == "height") {
					part.hasHeight = true;
					part.bottom = reader.number(child, "bottom", where);
					part.top = reader.number(child, "top", where);
				} else if (name == "top") {
					part.topColor = child.attribute("color").as_string();
					part.topDrawing = child.attribute("drawing").as_string();
				} else if (name == "sides") {
					part.hasSides = true;
					part.sideColor = child.attribute("color").as_string();
					part.sidesDrawing = child.attribute("drawing").as_string();
					for (const pugi::xml_node face : child.children()) {
						if (face.type() != pugi::node_element) {
							continue;
						}
						if (std::string_view(face.name()) != "face") {
							reader.unknown(face, where);
							continue;
						}
						part.faces.push_back({.sides = words(face.attribute("side").as_string()), .drawing = face.attribute("drawing").as_string()});
					}
				} else {
					reader.unknown(child, where);
				}
			}
			authored.parts.push_back(std::move(part));
		}

		for (const pugi::xml_node place : defNode.child("placements").children()) {
			if (place.type() != pugi::node_element) {
				continue;
			}
			if (std::string_view(place.name()) != "place") {
				reader.unknown(place, "<placements>");
				continue;
			}
			authored.placements.push_back(
				{.part = place.attribute("part").as_string(),
				 .x = reader.number(place, "x", "<placements>", 0.0F),
				 .y = reader.number(place, "y", "<placements>", 0.0F),
				 .turnDegrees = reader.number(place, "turnDegrees", "<placements>", 0.0F)}
			);
		}

		if (const pugi::xml_node useAreas = defNode.child("useAreas")) {
			std::vector<AuthoredArea>& areas = authored.useAreas.emplace();
			AuthoredArea			   unnamed;
			readAreaShapes(useAreas, unnamed, "<useAreas>", reader);
			if (!unnamed.shapes.empty()) {
				areas.push_back(std::move(unnamed));
			}
			for (const pugi::xml_node areaNode : useAreas.children("area")) {
				AuthoredArea& area = areas.emplace_back();
				area.name = areaNode.attribute("name").as_string();
				if (area.name.empty()) {
					reader.error("<useAreas>", "an <area> needs a name");
				}
				readAreaShapes(areaNode, area, "use area " + inQuotes(area.name), reader);
			}
		}

		if (const pugi::xml_node clearance = defNode.child("clearance")) {
			AuthoredArea& area = authored.clearance.emplace();
			readAreaShapes(clearance, area, "<clearance>", reader);
			// A margin of 0 is no margin, not the collider itself.
			if (const float margin = reader.number(clearance, "marginMeters", "<clearance>", 0.0F); margin != 0.0F) {
				area.shapes.insert(area.shapes.begin(), {.kind = AuthoredAreaShape::Kind::Around, .distanceMeters = margin});
			}
		}

		authored.scriptPath = defNode.child_value("partsScript");
		return authored;
	}

	ResolvedParts resolveParts(
		const PartsAuthored& authored, const std::filesystem::path& folder, const std::string& defName, const PartsLoadConfig& config,
		ValidationReport& report
	) {
		ResolvedParts resolved;
		PartsAsset&	  asset = resolved.asset;
		Resolver	  resolver(folder, defName, config, report);
		const auto	  error = [&](const std::string& field, const std::string& message, const std::string& context = "") {
			   resolver.error(field, message, context);
		};
		std::vector<DrawingUse> uses;

		// Parts: rules 2 (unique names), 3, 4 (heights, a flat part's sides), and 5.
		std::map<std::string, uint32_t>		  partIndex;
		std::vector<std::vector<std::string>> faceFiles; // per part, each face's drawing or empty for plain
		asset.parts.reserve(authored.parts.size());
		for (const AuthoredPart& written : authored.parts) {
			PartDefinition& part = asset.parts.emplace_back();
			part.name = written.name;
			const std::string where = "part " + inQuotes(written.name);
			if (written.name.empty()) {
				error("parts", "a part has no name");
			} else if (!partIndex.emplace(written.name, static_cast<uint32_t>(asset.parts.size() - 1)).second) {
				error("parts", "two parts are named " + inQuotes(written.name) + "; part names are unique");
			}

			if (written.footprint.shapeCount != 1) {
				error("parts", where + " needs one footprint, a <rect> or a <round>", where);
			}
			part.footprint = footprintPolygon(written.footprint);
			if (written.footprint.shapeCount == 1 && part.footprint.empty()) {
				error(
					"parts",
					written.footprint.shape == AuthoredFootprint::Shape::Rect ? where + "'s footprint rect needs max > min on both axes"
																			   : where + "'s footprint round needs a radius over 0 and a whole number of sides, 3 to 32",
					where
				);
			} else if (!part.footprint.empty() && thinnerThanOverlapTolerance(written.footprint)) {
				error(
					"parts",
					where + formatted(
								"'s footprint is thinner than the overlap tolerance; a rect needs both sides at least %g m, a round an inradius of at least %g m",
								kOverlapToleranceMeters,
								kOverlapToleranceMeters * 0.5
							),
					where
				);
				part.footprint.clear();
			}

			if (!written.hasHeight) {
				error("parts", where + " needs a <height bottom top>", where);
			} else if (!(written.bottom >= 0.0F && written.bottom <= written.top)) {
				error("parts", where + formatted(" has bottom %g and top %g; 0 <= bottom <= top", static_cast<double>(written.bottom), static_cast<double>(written.top)), where);
			} else {
				part.bottomMeters = written.bottom;
				part.topMeters = written.top;
			}
			const bool flat = part.flat();
			if (flat && written.hasSides) {
				error("parts", where + " is flat (top equals bottom), so it has no sides; drop its <sides>", where);
			}

			if (!part.footprint.empty()) {
				part.boundsMin = part.boundsMax = part.footprint.front();
				for (const glm::vec2& point : part.footprint) {
					part.boundsMin = glm::min(part.boundsMin, point);
					part.boundsMax = glm::max(part.boundsMax, point);
				}
			}

			if (const std::optional<Foundation::Color> color = parseRgbHexColor(written.topColor); color.has_value()) {
				part.topColor = *color;
			} else if (!written.topColor.empty()) {
				error("parts", where + "'s top color " + inQuotes(written.topColor) + " is not #RRGGBB", where);
			} else if (written.topDrawing.empty()) {
				error("parts", where + "'s <top> needs a color or a drawing", where);
			}
			if (!written.topDrawing.empty() && !part.footprint.empty()) {
				uses.push_back({.file = written.topDrawing, .coversMeters = part.boundsMax - part.boundsMin, .what = "the top of " + where, .target = &part.topDrawing});
			}

			// Faces: a drawing named by a <face>, else the <sides> drawing, else plain.
			const bool rect = written.footprint.shape == AuthoredFootprint::Shape::Rect;
			std::vector<std::string> faceDrawing(part.footprint.size(), flat ? std::string() : written.sidesDrawing);
			std::vector<bool>		 named(part.footprint.size(), false);
			for (const AuthoredFace& face : written.faces) {
				if (face.sides.empty()) {
					error("parts", where + " has a <face> that names no side", where);
				}
				if (face.drawing.empty()) {
					error("parts", where + " has a <face> with no drawing; a face without one takes the <sides> drawing or colour, so leave it out", where);
				}
				for (const std::string& side : face.sides) {
					const auto found = std::find(kRectFaceNames.begin(), kRectFaceNames.end(), side);
					if (!rect || found == kRectFaceNames.end()) {
						error(
							"parts",
							where + " has no face " + inQuotes(side) +
								(rect ? "; a rect's faces are front, back, left, and right" : "; a round's faces have no names, they all take the <sides> drawing"),
							where
						);
						continue;
					}
					const size_t index = static_cast<size_t>(found - kRectFaceNames.begin());
					if (index >= named.size()) {
						continue;
					}
					if (named[index]) {
						error("parts", where + " names face " + inQuotes(side) + " more than once", where);
					}
					named[index] = true;
					if (!face.drawing.empty()) {
						faceDrawing[index] = face.drawing;
					}
				}
			}
			if (!flat && !part.footprint.empty()) {
				const std::optional<Foundation::Color> sideColor = parseRgbHexColor(written.sideColor);
				if (sideColor.has_value()) {
					part.sideColor = *sideColor;
				} else if (!written.sideColor.empty()) {
					error("parts", where + "'s side color " + inQuotes(written.sideColor) + " is not #RRGGBB", where);
				} else if (std::any_of(faceDrawing.begin(), faceDrawing.end(), [](const std::string& file) { return file.empty(); })) {
					error("parts", where + " has a plain face, so its <sides> needs a color", where);
				}
				part.faces.resize(part.footprint.size());
				for (size_t i = 0; i < part.footprint.size(); ++i) {
					PartFace& face = part.faces[i];
					face.start = part.footprint[i];
					face.end = part.footprint[(i + 1) % part.footprint.size()];
					const glm::vec2 direction = face.end - face.start;
					face.lengthMeters = std::hypot(direction.x, direction.y);
					face.normal = glm::vec2{direction.y, -direction.x} / face.lengthMeters;
				}
			}
			faceFiles.push_back(std::move(faceDrawing));
		}
		// Face drawings are queued once every part's faces exist, so the targets stay put.
		for (size_t partNumber = 0; partNumber < asset.parts.size(); ++partNumber) {
			PartDefinition& part = asset.parts[partNumber];
			const bool		rect = authored.parts[partNumber].footprint.shape == AuthoredFootprint::Shape::Rect;
			for (size_t i = 0; i < part.faces.size(); ++i) {
				const std::string& file = faceFiles[partNumber][i];
				if (file.empty()) {
					continue;
				}
				const std::string faceName = rect ? std::string("face ") + kRectFaceNames[i] : formatted("face %zu", i);
				uses.push_back(
					{.file = file,
					 .coversMeters = {part.faces[i].lengthMeters, part.topMeters - part.bottomMeters},
					 .what = faceName + " of part " + inQuotes(part.name),
					 .target = &part.faces[i].drawing}
				);
			}
		}

		// Placements: rule 2.
		std::vector<bool> placed(asset.parts.size(), false);
		for (const AuthoredPlacement& written : authored.placements) {
			const auto found = partIndex.find(written.part);
			if (found == partIndex.end()) {
				error("placements", "a <place> names part " + inQuotes(written.part) + ", which the item has none of");
				continue;
			}
			placed[found->second] = true;
			asset.placements.push_back({.part = found->second, .offsetMeters = {written.x, written.y}, .turnRadians = written.turnDegrees * kPi / 180.0F});
		}
		if (authored.placements.empty()) {
			error("placements", "the item places no parts; it needs at least one <place>");
		}
		for (size_t partNumber = 0; partNumber < asset.parts.size(); ++partNumber) {
			if (!placed[partNumber] && !asset.parts[partNumber].name.empty()) {
				error("placements", "part " + inQuotes(asset.parts[partNumber].name) + " is placed nowhere");
			}
		}

		// Each placement's footprint in the item's frame, at turn 0.
		std::vector<Ring2>& placedFootprints = asset.placedFootprints;
		for (const PartPlacement& placement : asset.placements) {
			Ring2& ring = placedFootprints.emplace_back();
			for (const glm::vec2& point : asset.parts[placement.part].footprint) {
				ring.push_back(turnedPoint(point, placement.turnRadians) + placement.offsetMeters);
			}
		}
		const auto hasBody = [&](size_t placement) {
			return !placedFootprints[placement].empty() && !asset.parts[asset.placements[placement].part].flat();
		};

		// The collider (section 11.1): the union of the footprints with height, holes filled.
		std::vector<Ring2> bodyFootprints;
		for (size_t i = 0; i < asset.placements.size(); ++i) {
			if (hasBody(i)) {
				bodyFootprints.push_back(placedFootprints[i]);
				asset.bodyTopMeters = std::max(asset.bodyTopMeters, asset.parts[asset.placements[i].part].topMeters);
			}
		}
		std::vector<Ring2> colliderRings;
		if (!bodyFootprints.empty()) {
			const std::vector<AreaPolygon> body = unionInMeters({bodyFootprints});
			// Each outline in doubled millimetres, so the middle of an edge is a lattice point.
			std::vector<geometry::Ring> doubledOutlines;
			for (const AreaPolygon& polygon : body) {
				geometry::Ring& doubled = doubledOutlines.emplace_back(toMillimetres(polygon.outer));
				for (geometry::Vec2i64& point : doubled) {
					point = point * 2;
				}
			}
			for (size_t index = 0; index < body.size(); ++index) {
				const AreaPolygon& polygon = body[index];
				// A hole is filled, so an island inside another polygon's hole is inside its filled outline: no ring.
				// The probe is the middle of the island's first edge, strictly inside the other outline: a vertex
				// could be a pinch, which lies on the other outline's boundary.
				const geometry::Ring&	outline = doubledOutlines[index];
				const geometry::Vec2i64 probe{(outline[0].x + outline[1].x) / 2, (outline[0].y + outline[1].y) / 2};
				bool					island = false;
				for (size_t other = 0; other < body.size() && !island; ++other) {
					island = other != index && geometry::pointInPolygon(probe, doubledOutlines[other]) == geometry::PointInPolygon::Inside;
				}
				if (island) {
					continue;
				}
				colliderRings.push_back(polygon.outer);
				CollisionShape shape;
				shape.type = CollisionShapeType::Polygon;
				shape.pointsMeters = polygon.outer;
				resolved.colliders.push_back(std::move(shape));
			}
		}

		// Rule 4: a raised flat part rests, at every placement, on the footprints of parts with height below it.
		for (size_t i = 0; i < asset.placements.size(); ++i) {
			const PartDefinition& part = asset.parts[asset.placements[i].part];
			if (!part.flat() || part.bottomMeters <= 0.0F || placedFootprints[i].empty()) {
				continue;
			}
			std::vector<Ring2> supports;
			for (size_t j = 0; j < asset.placements.size(); ++j) {
				if (hasBody(j) && asset.parts[asset.placements[j].part].topMeters <= part.bottomMeters) {
					supports.push_back(placedFootprints[j]);
				}
			}
			const double supported = supports.empty() ? 0.0 : regionArea(unionInMeters({supports}));
			supports.push_back(placedFootprints[i]);
			const double withPart = regionArea(unionInMeters({supports}));
			if (withPart - supported > 1.0e-6) {
				error("parts", "part " + inQuotes(part.name) + formatted(" is flat and raised %g m, but rests on nothing there: ", static_cast<double>(part.bottomMeters)) +
								   "it must lie within the footprints of parts with height whose top is at or below its bottom");
			}
		}

		// Rule 9, first half: parts that overlap in plan are separated in height.
		const size_t					   count = asset.placements.size();
		asset.planOverlap.assign(count * count, false);
		asset.planOrder.assign(count * count, 0);
		for (size_t i = 0; i < count; ++i) {
			for (size_t j = i + 1; j < count; ++j) {
				if (placedFootprints[i].empty() || placedFootprints[j].empty() || !overlapInPlan(placedFootprints[i], placedFootprints[j])) {
					continue;
				}
				asset.planOverlap[i * count + j] = asset.planOverlap[j * count + i] = true;
				const PartDefinition& first = asset.parts[asset.placements[i].part];
				const PartDefinition& second = asset.parts[asset.placements[j].part];
				const bool			  firstAbove = first.bottomMeters >= second.topMeters;
				const bool			  secondAbove = second.bottomMeters >= first.topMeters;
				if (!firstAbove && !secondAbove) {
					error(
						"parts",
						"parts " + inQuotes(first.name) + " and " + inQuotes(second.name) + " overlap in plan and in height; one's bottom must be at or above the other's top"
					);
				} else if (firstAbove != secondAbove) {
					asset.planOrder[i * count + j] = static_cast<int8_t>(firstAbove ? 1 : -1);
					asset.planOrder[j * count + i] = static_cast<int8_t>(firstAbove ? -1 : 1);
				}
			}
		}

		// Rule 9, second half: the order of section 6.6 has no cycle at any turn. It changes only where a vertex of
		// one placement and one of another share a screen x, so it is checked once between each two such turns.
		if (count >= 3) {
			std::vector<float> turns;
			for (size_t i = 0; i < count; ++i) {
				for (size_t j = i + 1; j < count; ++j) {
					if (asset.overlapsInPlan(i, j)) {
						continue;
					}
					for (const glm::vec2& vertex : placedFootprints[i]) {
						for (const glm::vec2& otherVertex : placedFootprints[j]) {
							const glm::vec2 apart = vertex - otherVertex;
							if (apart.x == 0.0F && apart.y == 0.0F) {
								continue;
							}
							// x(t) = apart.x cos t - apart.y sin t vanishes at t = atan2(apart.x, apart.y), and half a turn on.
							float turn = std::atan2(apart.x, apart.y);
							for (int half = 0; half < 2; ++half, turn += kPi) {
								turns.push_back(std::fmod(turn + 4.0F * kPi, 2.0F * kPi));
							}
						}
					}
				}
			}
			std::sort(turns.begin(), turns.end());
			// Turns within 1e-6 radians are one: an interval that narrow holds no turn an item stands at, since a
			// placed turn is a whole number of 0.01 degree quanta (1.7e-4 radians).
			turns.erase(std::unique(turns.begin(), turns.end(), [](float earlier, float later) { return later - earlier < 1.0e-6F; }), turns.end());
			if (turns.empty()) {
				turns.push_back(0.0F);
			}
			std::vector<std::string> names;
			for (const PartPlacement& placement : asset.placements) {
				names.push_back(asset.parts[placement.part].name);
			}
			struct Run {
				float					 from = 0.0F;
				float					 to = 0.0F;
				std::vector<std::string> parts;
			};
			std::vector<Run> runs;
			for (size_t interval = 0; interval < turns.size(); ++interval) {
				const float from = turns[interval];
				const float to = interval + 1 < turns.size() ? turns[interval + 1] : turns.front() + 2.0F * kPi;
				const float middle = (from + to) * 0.5F;
				std::vector<Ring2> screen(count);
				for (size_t i = 0; i < count; ++i) {
					screen[i] = turnedFootprint(asset, i, middle);
				}
				const std::vector<std::vector<size_t>> later = partsDrawnAfter(asset, screen);
				std::vector<std::string> cycle = cycleIn(later, names);
				if (cycle.empty()) {
					continue;
				}
				if (!runs.empty() && runs.back().to == from && runs.back().parts == cycle) {
					runs.back().to = to;
				} else {
					runs.push_back({from, to, std::move(cycle)});
				}
			}
			for (const Run& run : runs) {
				std::string parts;
				for (const std::string& name : run.parts) {
					parts += (parts.empty() ? "" : ", ") + name;
				}
				const auto degrees = [](float radians) { return static_cast<double>(std::fmod(radians, 2.0F * kPi) * 180.0F / kPi); };
				error("parts", formatted("parts order cycles between %.1f and %.1f degrees: ", degrees(run.from), degrees(run.to)) + parts);
			}
		}

		// Areas (section 4.2), every shape as nonzero groups of rings in the item's frame.
		std::vector<Ring2> aroundBase = colliderRings;
		if (aroundBase.empty()) {
			std::vector<Ring2> footprints;
			for (const Ring2& ring : placedFootprints) {
				if (!ring.empty()) {
					footprints.push_back(ring);
				}
			}
			if (!footprints.empty()) {
				for (const AreaPolygon& polygon : unionInMeters({footprints})) {
					aroundBase.push_back(polygon.outer);
				}
			}
		}
		struct PendingArea {
			std::string						name;
			std::string						field;
			std::vector<std::vector<Ring2>> groups;
		};
		const auto shapesOf = [&](const AuthoredArea& area, const std::string& field, const std::string& where) {
			PendingArea pending{.name = area.name, .field = field, .groups = {}};
			for (const AuthoredAreaShape& shape : area.shapes) {
				switch (shape.kind) {
					case AuthoredAreaShape::Kind::Around:
						if (shape.distanceMeters < 0.0F) {
							error(field, where + formatted(" grows the collider by %g m; the distance can't be negative", static_cast<double>(shape.distanceMeters)));
						} else {
							appendGrown(aroundBase, shape.distanceMeters, config.curveToleranceMeters, pending.groups);
						}
						break;
					case AuthoredAreaShape::Kind::Circle:
						if (!(shape.radius > 0.0F)) {
							error(field, where + " has a circle with no radius; it needs a radius over 0");
						} else {
							pending.groups.push_back({disc({shape.x, shape.y}, shape.radius, config.curveToleranceMeters)});
						}
						break;
					case AuthoredAreaShape::Kind::Rect:
					case AuthoredAreaShape::Kind::Drawing:
						if (!(shape.maxX > shape.minX && shape.maxY > shape.minY)) {
							error(field, where + " has a " + (shape.kind == AuthoredAreaShape::Kind::Rect ? "rect" : "drawing") + " whose max isn't above its min on both axes");
						} else if (shape.kind == AuthoredAreaShape::Kind::Rect) {
							pending.groups.push_back({{{shape.minX, shape.minY}, {shape.maxX, shape.minY}, {shape.maxX, shape.maxY}, {shape.minX, shape.maxY}}});
						}
						break;
				}
			}
			return pending;
		};

		std::vector<PendingArea> pendingUse;
		if (authored.useAreas.has_value()) {
			// One or more areas: named ones, each name once, or a single unnamed one. A script's are checked alike.
			const std::vector<AuthoredArea>& areas = *authored.useAreas;
			if (areas.empty()) {
				error("useAreas", "<useAreas> holds no area; it holds one or more, or leave it out for the default");
			}
			const bool anyUnnamed = std::any_of(areas.begin(), areas.end(), [](const AuthoredArea& area) { return area.name.empty(); });
			if (anyUnnamed && areas.size() > 1) {
				error("useAreas", "<useAreas> holds either named <area>s or the shapes of one unnamed area, not both");
			}
			std::set<std::string> names;
			for (const AuthoredArea& area : areas) {
				if (!area.name.empty() && !names.insert(area.name).second) {
					error("useAreas", "two use areas are named " + inQuotes(area.name) + "; area names are unique");
				}
				if (area.shapes.empty()) {
					error("useAreas", (area.name.empty() ? std::string("the use area") : "use area " + inQuotes(area.name)) + " has no shapes");
				}
			}
			for (const AuthoredArea& area : areas) {
				pendingUse.push_back(shapesOf(area, "useAreas", area.name.empty() ? std::string("the use area") : "use area " + inQuotes(area.name)));
			}
		} else {
			AuthoredArea defaultArea;
			defaultArea.shapes.push_back({.kind = AuthoredAreaShape::Kind::Around, .distanceMeters = config.defaultUseDistanceMeters});
			pendingUse.push_back(shapesOf(defaultArea, "useAreas", "the default use area"));
		}
		std::optional<PendingArea> pendingClearance;
		if (authored.clearance.has_value()) {
			pendingClearance = shapesOf(*authored.clearance, "clearance", "the clearance");
		}

		// Area drawings, read with the parts' drawings so a file used twice is read once.
		const auto queueAreaDrawings = [&](const AuthoredArea& area, PendingArea& pending, const std::string& where) {
			for (const AuthoredAreaShape& shape : area.shapes) {
				if (shape.kind == AuthoredAreaShape::Kind::Drawing && !shape.file.empty() && shape.maxX > shape.minX && shape.maxY > shape.minY) {
					uses.push_back(
						{.file = shape.file,
						 .coversMeters = {shape.maxX - shape.minX, shape.maxY - shape.minY},
						 .what = where + "'s drawing rectangle",
						 .fillGroups = &pending.groups,
						 .originMeters = {shape.minX, shape.minY}}
					);
				}
			}
		};
		if (authored.useAreas.has_value()) {
			for (size_t i = 0; i < authored.useAreas->size(); ++i) {
				const AuthoredArea& area = (*authored.useAreas)[i];
				queueAreaDrawings(area, pendingUse[i], area.name.empty() ? std::string("the use area") : "use area " + inQuotes(area.name));
			}
		}
		if (pendingClearance.has_value()) {
			queueAreaDrawings(*authored.clearance, *pendingClearance, "the clearance");
		}

		resolver.loadDrawings(uses, asset.drawings);

		for (PendingArea& pending : pendingUse) {
			ItemArea area{.name = pending.name, .polygons = unionInMeters(pending.groups)};
			const std::string where = pending.name.empty() ? std::string("the use area") : "use area " + inQuotes(pending.name);
			// Rule 11: one colonist fits, on open ground beside nothing but the item itself.
			if (standingRegion(area.polygons, colliderRings, nullptr, kLargestColonistRadiusMeters).empty()) {
				error(
					"useAreas",
					where + formatted(" has no room: no disc of %g m radius fits in it clear of the item's collider, so no colonist could ever use the item from it", static_cast<double>(kLargestColonistRadiusMeters))
				);
			}
			asset.useAreas.push_back(std::move(area));
		}
		if (pendingClearance.has_value()) {
			asset.clearance = {.name = {}, .polygons = unionInMeters(pendingClearance->groups)};
		}
		return resolved;
	}

} // namespace assets
