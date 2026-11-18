#pragma once

// One colonist's drawn shadows (sight-softening.md 3.3, 7.3): from his position each frame, over the
// bodies of his last build, a triangle and two fins per soft body and a band and a fin per shadow edge,
// in world millimetres. The band's apex eases along a body face over shadowCornerEaseMm of his walk
// when the corner it is followed to changes (3.1, the corner switch), kept per shadow edge by (body,
// litTurn, corner). Drawn only, never read by the sim.

#include "CoverPieces.h"

#include <ecs/systems/VisionSystem.h>

#include <vision/SightTunables.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace worldview {

	class DrawnShadows {
	  public:
		// One shadow edge as drawn this frame.
		struct EdgeDrawn {
			std::uint32_t body			 = 0; // into the build's bodies
			std::uint64_t bodyId		 = 0; // its serial or wall body, and its diameter
			std::int64_t  diameterMm	 = 0;
			std::uint32_t buildCorner	 = 0;
			std::uint32_t followedCorner = 0;
			ShadowBand	  band;
			double		  ease = 1.0; // how far the apex has come from its old corner to the followed one
		};
		// One soft body as drawn this frame.
		struct SoftDrawn {
			std::uint32_t body		 = 0;
			std::uint64_t bodyId	 = 0;
			std::int64_t  diameterMm = 0;
			SoftShadow	  shadow;
		};
		struct Counts {
			std::size_t softBodies	  = 0;
			std::size_t shadowEdges	  = 0;
			std::size_t pieces		  = 0;
			bool		rebuilt		  = false;
		};

		// The frame with him at `eyeMm`, having walked `walkedMm` since the last, over `bodies` of the
		// build `buildRevision`. Bodies farther than the least of drawnShadowReachMm, the build's reach, and
		// `brightReachMm`, the farthest his bright shape reaches from him, draw nothing: every piece is clipped
		// to that shape. With drawnShadowRebuildMm above 0 the
		// pieces are kept until he has moved that far from where they were built, or a build or the
		// tunables change.
		void update(
			const ecs::VisionSystem::SightBodies& bodies,
			std::uint64_t						  buildRevision,
			glm::dvec2							  eyeMm,
			double								  walkedMm,
			double								  brightReachMm,
			const vision::SightTunables&		  tunables
		);

		[[nodiscard]] const std::vector<ValuePolygon>& pieces() const { return drawn; }
		[[nodiscard]] const std::vector<EdgeDrawn>&	   edges() const { return edgesDrawn; }
		[[nodiscard]] const std::vector<SoftDrawn>&	   softBodies() const { return softDrawn; }
		[[nodiscard]] const Counts&					   counts() const { return frameCounts; }

	  private:
		// One shadow edge's apex: where it is drawn, where its ease started, the corner it eases to, and
		// his walk since that corner was taken.
		struct CornerEase {
			vision::SightBody::Source source = vision::SightBody::Source::Placed;
			std::uint64_t			  bodyId = 0;
			int						  litTurn = 1;
			geometry::Vec2i64		  buildCorner{0, 0};
			glm::dvec2				  apex{0.0};
			glm::dvec2				  from{0.0};
			glm::dvec2				  target{0.0};
			double					  walkedMm = 0.0;
			bool					  started  = false;
		};

		// The build's shadow edges take up the running eases of the last build's (3.1).
		void takeUpEases(const ecs::VisionSystem::SightBodies& bodies);

		std::uint64_t			lastBuild = 0;
		bool					built	  = false;
		std::vector<CornerEase> eases; // one a shadow edge of the build, in its order
		glm::dvec2				builtAt{0.0};
		vision::SightTunables	builtWith;
		std::vector<ValuePolygon> drawn;
		std::vector<EdgeDrawn>	  edgesDrawn;
		std::vector<SoftDrawn>	  softDrawn;
		Counts					  frameCounts;
	};

} // namespace worldview
