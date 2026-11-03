#pragma once

// A ground def's <macro> block (docs/technical/ground-textures.md, "Shader"): the low-frequency tint the
// tile shader mixes each surface's color toward, dry or lush, by the per-chunk macro texture. The loader
// and AssetValidator read it through one function, readGroundMacro.

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace pugi {
	class xml_node;
}

namespace assets {

	/// The surface color is mixed toward `dry` or `lush` by a smooth noise field. Amplitude 0 (the default)
	/// leaves it off.
	struct GroundMacro {
		static constexpr float kMaxTint = 2.0F;		 // dry and lush multiply the sRGB color, each channel 0 to this
		static constexpr float kMaxAmplitude = 1.0F; // 1 tints all the way to dry and lush

		glm::vec3 dry{1.0F, 1.0F, 1.0F};		   // Tint where the field is low, r,g,b
		glm::vec3 lush{1.0F, 1.0F, 1.0F};		   // Tint where the field is high, r,g,b
		float	  amplitude = 0.0F;				   // Strength of the tint; 0 turns it off
		glm::vec2 wavelengthsMeters{96.0F, 24.0F}; // Coarse and fine noise wavelengths, meters (not read yet)
	};

	/// A <macro> child that breaks the schema: which one, why, and the value as written.
	struct GroundMacroIssue {
		std::string field;
		std::string message;
		std::string value;
	};

	/// The <macro> block of a ground def node. A child left out or empty keeps its default; one that isn't
	/// the right count of finite numbers in range keeps it too, with an issue in `issues`.
	GroundMacro readGroundMacro(const pugi::xml_node& defNode, std::vector<GroundMacroIssue>& issues);

} // namespace assets
