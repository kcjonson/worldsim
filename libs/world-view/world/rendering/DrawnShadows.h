#pragma once

// One colonist's drawn shadows (sight-softening.md 3.3, 7.3), in world millimetres, over the bodies of
// his last build: a triangle and two fins per soft body, and a band and a fin per shadow edge. The
// band's apex eases along a body face over shadowCornerEaseMm of his walk when the corner it is
// followed to changes (3.1, the corner switch), kept per shadow edge by (body, litTurn, corner).
//
// The two are built on their own cadences. The soft triangles are many and faint: they are rebuilt
// once he has walked drawnShadowRebuildMm from where they were built, and drawn as built until then,
// anchored to their bodies. The bands are few and hold the sim's shadow side to limit 8: they are
// rebuilt on every frame he moves, a new build arrives, or an apex is still easing, and not otherwise.
// Drawn only, never read by the sim.

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
		// One soft body as drawn at the last soft rebuild.
		struct SoftDrawn {
			std::uint64_t bodyId	 = 0;
			std::int64_t  diameterMm = 0;
			SoftShadow	  shadow;
		};
		// What the last update rebuilt.
		struct Counts {
			bool softRebuilt  = false;
			bool bandsRebuilt = false;
		};

		// Whether his soft triangles are due a rebuild with him at `eyeMm`: none built yet, the tunables
		// changed, or he is drawnShadowRebuildMm or more from where they were built (every frame at 0).
		[[nodiscard]] bool softDue(glm::dvec2 eyeMm, const vision::SightTunables& tunables) const;

		// The frame with him at `eyeMm`, having walked `walkedMm` since the last, over `bodies` of the build
		// `buildRevision`; the soft triangles rebuilt when `rebuildSoft`, which the cover decides from
		// softDue and its budget a frame. Bodies farther than the least of drawnShadowReachMm, the build's
		// reach, and `brightReachMm`, the farthest his bright shape reaches from him, draw nothing: every
		// piece is clipped to that shape.
		void update(
			const ecs::VisionSystem::SightBodies& bodies,
			std::uint64_t						  buildRevision,
			glm::dvec2							  eyeMm,
			double								  walkedMm,
			double								  brightReachMm,
			const vision::SightTunables&		  tunables,
			bool								  rebuildSoft
		);

		[[nodiscard]] const std::vector<ValueTriangle>& softPieces() const { return softTriangles; }
		[[nodiscard]] const std::vector<ValueTriangle>& bandPieces() const { return bandTriangles; }
		[[nodiscard]] const std::vector<EdgeDrawn>&		edges() const { return edgesDrawn; }
		[[nodiscard]] const std::vector<SoftDrawn>&		softBodies() const { return softDrawn; }
		[[nodiscard]] const Counts&						counts() const { return frameCounts; }
		// Where the soft triangles were last built.
		[[nodiscard]] glm::dvec2 softBuiltAt() const { return softEye; }

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
		void buildSoft(const ecs::VisionSystem::SightBodies& bodies, glm::dvec2 eyeMm, double reachLimit, const vision::SightTunables& tunables);
		// Moves each apex on and rebuilds the bands; false when nothing they draw has changed.
		bool buildBands(const ecs::VisionSystem::SightBodies& bodies, glm::dvec2 eyeMm, double reachLimit, double brightReachMm, const vision::SightTunables& tunables, bool force, double walkedMm);

		std::uint64_t			  lastBuild = 0;
		bool					  built		= false;
		std::vector<CornerEase>	  eases; // one a shadow edge of the build, in its order
		bool					  softBuilt = false;
		glm::dvec2				  softEye{0.0};
		vision::SightTunables	  softTunables;
		bool					  bandsBuilt = false;
		glm::dvec2				  bandEye{0.0};
		vision::SightTunables	  bandTunables;
		std::vector<ValueTriangle> softTriangles;
		std::vector<ValueTriangle> bandTriangles;
		std::vector<EdgeDrawn>	   edgesDrawn;
		std::vector<SoftDrawn>	   softDrawn;
		Counts					   frameCounts;
	};

} // namespace worldview
