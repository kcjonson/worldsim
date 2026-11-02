#include "assets/GroundTransition.h"

#include <world/chunk/Chunk.h>
#include <world/chunk/SurfaceField.h>

#include <pugixml.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string_view>

namespace assets {

	static_assert(kGroundSurfaceCount == static_cast<size_t>(world::Surface::Count));

	namespace {

		struct TransitionField {
			const char* name;
			float GroundTransition::* member;
			float					  min;
			float					  max;
		};

		// The schema (ground-textures.md, "Pair parameters"). Under 0.03 of softness an 8-bit height steps
		// visibly when magnified; the offset is held to the width by checkBand.
		constexpr std::array<TransitionField, 8> kFields = {{
			{"width", &GroundTransition::widthM, 0.05F, 1.4F},
			{"offset", &GroundTransition::offsetM, -1.4F, 1.4F},
			{"softness", &GroundTransition::softness, 0.03F, 0.3F},
			{"breakup", &GroundTransition::breakup, 0.0F, 0.8F},
			{"shadow", &GroundTransition::shadow, 0.0F, 1.0F},
			{"stain", &GroundTransition::stain, 0.0F, 1.0F},
			{"ridge", &GroundTransition::ridge, 0.0F, 1.0F},
			{"tint", &GroundTransition::tint, 0.0F, 1.0F},
		}};

		// Floats that should land exactly on the reach, like 1.2 + 0.2, may round past it.
		constexpr float kReachSlack = 1.0e-4F;

		std::string number(float value) {
			char text[32];
			std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
			return text;
		}

		/// The element's text as a finite number, surrounding whitespace allowed.
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

		/// Lays the block's elements over `t`; false when one isn't a number in its range.
		bool readBlock(const pugi::xml_node& block, const std::string& context, GroundTransition& t, std::vector<GroundTransitionIssue>& issues) {
			bool ok = true;
			for (const pugi::xml_node child : block.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const TransitionField* field = nullptr;
				for (const TransitionField& f : kFields) {
					if (std::string_view(child.name()) == f.name) {
						field = &f;
					}
				}
				if (field == nullptr) {
					issues.push_back({false, std::string("<") + child.name() + "> is not a transition field; it has no effect", context});
					continue;
				}
				const char*					text = child.child_value();
				const std::optional<float> value = parseNumber(text);
				if (!value.has_value()) {
					issues.push_back({true, std::string("<") + field->name + "> is not a number", context + ": '" + text + "'"});
					ok = false;
					continue;
				}
				if (*value < field->min || *value > field->max) {
					issues.push_back(
						{true,
						 std::string("<") + field->name + "> must be from " + number(field->min) + " to " + number(field->max),
						 context + ": " + number(*value)}
					);
					ok = false;
					continue;
				}
				t.*(field->member) = *value;
			}
			return ok;
		}

		/// The band as resolved (an override's fields over the default's): the offset inside the width, and
		/// both within the field's reach.
		bool checkBand(const GroundTransition& t, const std::string& context, std::vector<GroundTransitionIssue>& issues) {
			if (std::abs(t.offsetM) >= t.widthM) {
				issues.push_back(
					{true, "<offset> must lie strictly between -width and width", context + ": offset " + number(t.offsetM) + ", width " + number(t.widthM)}
				);
				return false;
			}
			const float reach = t.widthM + std::abs(t.offsetM);
			if (reach > kGroundBandReachM + kReachSlack) {
				issues.push_back(
					{true,
					 "width + |offset| must be at most " + number(kGroundBandReachM) + " m, as far as the land field reports an edge",
					 context + ": " + number(reach) + " m"}
				);
				return false;
			}
			return true;
		}

	} // namespace

	GroundTransitions
	readGroundTransitions(const pugi::xml_node& defNode, std::optional<world::Surface> surface, std::vector<GroundTransitionIssue>& issues) {
		GroundTransition defaults;
		bool			 sawDefault = false;
		for (const pugi::xml_node block : defNode.children("transition")) {
			if (block.attribute("over")) {
				continue;
			}
			if (sawDefault) {
				issues.push_back({true, "A second default <transition>; only the first is read", "default"});
				continue;
			}
			sawDefault = true;
			GroundTransition t = defaults;
			if (readBlock(block, "default", t, issues) && checkBand(t, "default", issues)) {
				defaults = t;
			}
		}

		GroundTransitions out;
		out.fill(defaults);
		std::array<bool, kGroundSurfaceCount> overridden{};
		const int							  ownLevel = surface.has_value() ? world::surfaceStackLevel(static_cast<uint8_t>(*surface)) : 0;
		for (const pugi::xml_node block : defNode.children("transition")) {
			const pugi::xml_attribute over = block.attribute("over");
			if (!over) {
				continue;
			}
			const std::string name = over.value();
			const std::string context = "over=\"" + name + "\"";
			const auto		  lower = world::surfaceFromString(name);
			if (!lower.has_value() || *lower == world::Surface::Water) {
				issues.push_back({true, "over must name a land surface", context});
				continue;
			}
			const auto id = static_cast<uint8_t>(*lower);
			if (surface.has_value() && world::surfaceStackLevel(id) >= ownLevel) {
				issues.push_back(
					{true, "over must name a surface below this one in the D13 order: Mud, Sand, Dirt, GrassShort, Grass, GrassMeadow, GrassTall, Rock, Snow", context}
				);
				continue;
			}
			if (overridden[id]) {
				issues.push_back({true, "A second <transition> over this surface; only the first is read", context});
				continue;
			}
			overridden[id] = true;
			GroundTransition t = defaults;
			if (readBlock(block, context, t, issues) && checkBand(t, context, issues)) {
				out[id] = t;
			}
		}
		return out;
	}

	std::optional<std::string> groundBakeXml(const std::filesystem::path& file) {
		pugi::xml_document doc;
		if (!doc.load_file(file.c_str())) {
			return std::nullopt;
		}
		for (pugi::xml_node def : doc.child("AssetDefinitions").children("AssetDef")) {
			while (const pugi::xml_node block = def.child("transition")) {
				def.remove_child(block);
			}
		}
		std::ostringstream out;
		doc.save(out, "", pugi::format_raw);
		return out.str();
	}

} // namespace assets
