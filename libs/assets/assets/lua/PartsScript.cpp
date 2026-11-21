// A parts def's <partsScript> (rotatable-placed-items.md, section 4.5): runs once at load on what the XML wrote,
// before validation, and may add parts, placements, and area shapes and change a part's footprint or heights.
// Tables take the XML's fields as keys:
//   asset:addPart{ name = "leg", footprint = { rect = { minX = .., minY = .., maxX = .., maxY = .. } },
//                  height = { bottom = 0, top = 0.4 }, top = { color = "#9A6A3F" },
//                  sides = { color = "#9A6A3F", drawing = "x.svg", faces = { { side = "left right", drawing = "end.svg" } } } }
//   asset:place{ part = "leg", x = 0.2, y = 0.2, turnDegrees = 0 }
//   asset:addUseArea{ name = "front", shapes = { { rect = {..} }, { circle = {..} }, { around = { distanceMeters = .. } },
//                     { drawing = { file = .., minX = .., .. } } } }
//   asset:addClearance{ marginMeters = 0.2, shapes = {..} }
//   asset:setFootprint("leg", { round = { x = 0, y = 0, radius = 0.05, sides = 8 } })
//   asset:setHeight("leg", { bottom = 0, top = 0.5 })
// It never draws: drawings are files.

#include "assets/PartsAsset.h"
#include "assets/lua/LuaEngine.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace assets {

	namespace {

		float field(const sol::table& table, const char* key, std::optional<float> fallback = std::nullopt) {
			const sol::optional<double> value = table[key];
			if (!value.has_value()) {
				if (!fallback.has_value()) {
					throw std::runtime_error(std::string("missing number '") + key + "'");
				}
				return *fallback;
			}
			return static_cast<float>(*value);
		}

		std::string text(const sol::table& table, const char* key) {
			const sol::optional<std::string> value = table[key];
			return value.value_or("");
		}

		AuthoredFootprint footprintFrom(const sol::table& table) {
			AuthoredFootprint footprint;
			if (const sol::optional<sol::table> rect = table["rect"]; rect.has_value()) {
				footprint.shape = AuthoredFootprint::Shape::Rect;
				footprint.minX = field(*rect, "minX");
				footprint.minY = field(*rect, "minY");
				footprint.maxX = field(*rect, "maxX");
				footprint.maxY = field(*rect, "maxY");
				++footprint.shapeCount;
			}
			if (const sol::optional<sol::table> round = table["round"]; round.has_value()) {
				footprint.shape = AuthoredFootprint::Shape::Round;
				footprint.x = field(*round, "x", 0.0F);
				footprint.y = field(*round, "y", 0.0F);
				footprint.radius = field(*round, "radius");
				footprint.sides = static_cast<int32_t>(field(*round, "sides"));
				++footprint.shapeCount;
			}
			return footprint;
		}

		void appendShapes(const sol::table& table, AuthoredArea& area) {
			const sol::optional<sol::table> shapes = table["shapes"];
			if (!shapes.has_value()) {
				return;
			}
			for (const auto& [index, entry] : *shapes) {
				const sol::table  shape = entry.as<sol::table>();
				AuthoredAreaShape out;
				if (const sol::optional<sol::table> around = shape["around"]; around.has_value()) {
					out.kind = AuthoredAreaShape::Kind::Around;
					out.distanceMeters = field(*around, "distanceMeters");
				} else if (const sol::optional<sol::table> circle = shape["circle"]; circle.has_value()) {
					out.kind = AuthoredAreaShape::Kind::Circle;
					out.x = field(*circle, "x", 0.0F);
					out.y = field(*circle, "y", 0.0F);
					out.radius = field(*circle, "radius");
				} else {
					const sol::optional<sol::table> rect = shape["rect"];
					const sol::optional<sol::table> drawing = shape["drawing"];
					const sol::table				 bounds = rect.has_value() ? *rect : drawing.value_or(sol::table());
					if (!rect.has_value() && !drawing.has_value()) {
						throw std::runtime_error("an area shape is one of around, circle, rect, or drawing");
					}
					out.kind = rect.has_value() ? AuthoredAreaShape::Kind::Rect : AuthoredAreaShape::Kind::Drawing;
					out.minX = field(bounds, "minX");
					out.minY = field(bounds, "minY");
					out.maxX = field(bounds, "maxX");
					out.maxY = field(bounds, "maxY");
					out.file = text(bounds, "file");
				}
				area.shapes.push_back(std::move(out));
			}
		}

		AuthoredPart& partNamed(PartsAuthored& authored, const std::string& name) {
			for (AuthoredPart& part : authored.parts) {
				if (part.name == name) {
					return part;
				}
			}
			throw std::runtime_error("no part named '" + name + "'");
		}

	} // namespace

	std::string runPartsScript(const std::filesystem::path& script, PartsAuthored& authored) {
		std::ifstream in(script, std::ios::binary);
		if (!in) {
			return "parts script " + script.string() + " does not exist";
		}
		const std::string source{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

		sol::state lua;
		lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
		LuaEngine::sandbox(lua);

		sol::table asset = lua.create_named_table("asset");
		asset.set_function("addPart", [&authored](const sol::table&, const sol::table& table) {
			AuthoredPart part;
			part.name = text(table, "name");
			if (const sol::optional<sol::table> footprint = table["footprint"]; footprint.has_value()) {
				part.footprint = footprintFrom(*footprint);
			}
			if (const sol::optional<sol::table> height = table["height"]; height.has_value()) {
				part.hasHeight = true;
				part.bottom = field(*height, "bottom");
				part.top = field(*height, "top");
			}
			if (const sol::optional<sol::table> top = table["top"]; top.has_value()) {
				part.topColor = text(*top, "color");
				part.topDrawing = text(*top, "drawing");
			}
			if (const sol::optional<sol::table> sides = table["sides"]; sides.has_value()) {
				part.hasSides = true;
				part.sideColor = text(*sides, "color");
				part.sidesDrawing = text(*sides, "drawing");
				if (const sol::optional<sol::table> faces = (*sides)["faces"]; faces.has_value()) {
					for (const auto& [index, entry] : *faces) {
						const sol::table		 face = entry.as<sol::table>();
						std::istringstream		 names(text(face, "side"));
						AuthoredFace			 out{.sides = {}, .drawing = text(face, "drawing")};
						for (std::string name; names >> name;) {
							out.sides.push_back(name);
						}
						part.faces.push_back(std::move(out));
					}
				}
			}
			authored.parts.push_back(std::move(part));
		});
		asset.set_function("place", [&authored](const sol::table&, const sol::table& table) {
			authored.placements.push_back(
				{.part = text(table, "part"), .x = field(table, "x", 0.0F), .y = field(table, "y", 0.0F), .turnDegrees = field(table, "turnDegrees", 0.0F)}
			);
		});
		asset.set_function("addUseArea", [&authored](const sol::table&, const sol::table& table) {
			AuthoredArea area;
			area.name = text(table, "name");
			appendShapes(table, area);
			if (!authored.useAreas.has_value()) {
				authored.useAreas.emplace();
			}
			authored.useAreas->push_back(std::move(area));
		});
		asset.set_function("addClearance", [&authored](const sol::table&, const sol::table& table) {
			AuthoredArea& clearance = authored.clearance.has_value() ? *authored.clearance : authored.clearance.emplace();
			if (const sol::optional<double> margin = table["marginMeters"]; margin.has_value()) {
				clearance.shapes.push_back({.kind = AuthoredAreaShape::Kind::Around, .distanceMeters = static_cast<float>(*margin)});
			}
			appendShapes(table, clearance);
		});
		asset.set_function("setFootprint", [&authored](const sol::table&, const std::string& name, const sol::table& table) {
			partNamed(authored, name).footprint = footprintFrom(table);
		});
		asset.set_function("setHeight", [&authored](const sol::table&, const std::string& name, const sol::table& table) {
			AuthoredPart& part = partNamed(authored, name);
			part.hasHeight = true;
			part.bottom = field(table, "bottom");
			part.top = field(table, "top");
		});

		const sol::protected_function_result result = lua.safe_script(source, sol::script_pass_on_error, script.string());
		if (!result.valid()) {
			const sol::error failure = result;
			return failure.what();
		}
		return {};
	}

} // namespace assets
