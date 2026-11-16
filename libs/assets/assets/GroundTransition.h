#pragma once

// How a ground surface meets the surfaces below it in the D13 order (docs/technical/ground-textures.md,
// "Pair parameters"): a ground def's <transition> blocks, a default and an override per lower neighbor,
// read into the land shader's height contest and shading for every pair the def's surface is the upper of.
// The bake never reads them, so they stay out of its cache key.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pugi {
	class xml_node;
}

namespace world {
	enum class Surface : uint8_t;
}

namespace assets {

	/// One pair's band and shading, as the upper surface's def gives them.
	struct GroundTransition {
		float widthM = 0.4F;   ///< band half-width, read-space meters from the isoline
		float offsetM = 0.0F;  ///< positive spills this surface over the lower one, negative the lower one over this
		float softness = 0.1F; ///< the height window with no mip term
		float breakup = 0.5F;  ///< peak-to-peak height of the along-edge noise
		float shadow = 0.0F;   ///< contact shadow on the lower surface under this one's edge
		float stain = 0.0F;	   ///< darkens this surface toward the edge
		float ridge = 0.0F;	   ///< lightens a line near this surface's outer reach
		float tint = 0.0F;	   ///< veils the lower surface with this one's color

		bool operator==(const GroundTransition&) const = default;
	};

	/// world::Surface::Count, which this header can't see (GroundTransition.cpp checks it).
	inline constexpr size_t kGroundSurfaceCount = 10;

	/// A def's transitions by the lower surface's id, each starting from the def's default block.
	using GroundTransitions = std::array<GroundTransition, kGroundSurfaceCount>;

	/// The farthest from its isoline the land field reports an edge, read-space meters: a tile of blur and
	/// half a tile of bilinear read (SurfaceField.h). A band's width plus its offset stays within it.
	inline constexpr float kGroundBandReachM = 1.5F;

	struct GroundTransitionIssue {
		bool		error = true; ///< else a warning
		std::string message;
		std::string context; ///< which block (`default` or `over="Sand"`), and the value
	};

	/// The <transition> children of a ground def painting `surface` (unset when its <surface> didn't parse,
	/// which skips the checks that need it). Each override starts from the default block. A block that
	/// breaks a rule is dropped, with an error in `issues`, and its pair keeps the default; an element the
	/// schema doesn't know is a warning and is ignored.
	GroundTransitions
	readGroundTransitions(const pugi::xml_node& defNode, std::optional<world::Surface> surface, std::vector<GroundTransitionIssue>& issues);

} // namespace assets
