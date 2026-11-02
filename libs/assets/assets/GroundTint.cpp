#include "assets/GroundTint.h"

#include <pugixml.hpp>

#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace assets {

	namespace {

		// Exactly N comma-separated finite numbers, spaces allowed around each; false for anything else,
		// a trailing extra component included.
		template <size_t N>
		bool parseNumbers(std::string_view text, std::array<float, N>& out) {
			size_t at = 0;
			for (size_t i = 0; i < N; ++i) {
				const size_t comma = text.find(',', at);
				const bool	 last = i + 1 == N;
				if (last != (comma == std::string_view::npos)) {
					return false;
				}
				const std::string part(text.substr(at, last ? std::string_view::npos : comma - at));
				char*			  end = nullptr;
				const float		  value = std::strtof(part.c_str(), &end);
				if (end == part.c_str()) {
					return false;
				}
				while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
					++end;
				}
				if (*end != '\0' || !std::isfinite(value)) {
					return false;
				}
				out[i] = value;
				at = comma + 1;
			}
			return true;
		}

	} // namespace

	GroundMacro readGroundMacro(const pugi::xml_node& defNode, std::vector<GroundMacroIssue>& issues) {
		GroundMacro			 macro;
		const pugi::xml_node node = defNode.child("macro");
		if (!node) {
			return macro;
		}

		const auto tint = [&](const char* field, glm::vec3& member) {
			const std::string	 text = node.child_value(field);
			std::array<float, 3> rgb{};
			if (text.empty()) {
				return;
			}
			if (!parseNumbers(text, rgb)) {
				issues.push_back({field, "must be three numbers, r,g,b", text});
				return;
			}
			for (const float c : rgb) {
				if (c < 0.0F || c > GroundMacro::kMaxTint) {
					issues.push_back({field, "channels must be from 0 to 2", text});
					return;
				}
			}
			member = {rgb[0], rgb[1], rgb[2]};
		};
		tint("dry", macro.dry);
		tint("lush", macro.lush);

		if (const std::string text = node.child_value("amplitude"); !text.empty()) {
			std::array<float, 1> amplitude{};
			if (!parseNumbers(text, amplitude)) {
				issues.push_back({"amplitude", "must be a number", text});
			} else if (amplitude[0] < 0.0F || amplitude[0] > GroundMacro::kMaxAmplitude) {
				issues.push_back({"amplitude", "must be from 0 to 1", text});
			} else {
				macro.amplitude = amplitude[0];
			}
		}

		if (const std::string text = node.child_value("wavelengths"); !text.empty()) {
			std::array<float, 2> lengths{};
			if (!parseNumbers(text, lengths)) {
				issues.push_back({"wavelengths", "must be two numbers, coarse,fine (meters)", text});
			} else if (!(lengths[0] > 0.0F && lengths[1] > 0.0F)) {
				issues.push_back({"wavelengths", "must both be above 0", text});
			} else {
				macro.wavelengthsMeters = {lengths[0], lengths[1]};
			}
		}
		return macro;
	}

} // namespace assets
