#pragma once

// One colonist's drawn shadows (sight-softening.md 3.3, 7.3), in world millimetres, over the bodies of
// his last build: a triangle and two fins per soft body, and a band and a fin per shadow edge. The
// band's apex eases along a body face over shadowCornerEaseMm of his walk when the corner it is
// followed to changes (3.1, the corner switch), kept per shadow edge by (body, litTurn, corner).
//
// Each soft body's pieces are kept on their own, built from where he stood, and fall due again when he
// has walked softShadowRebuildPercent of that body's distance since (near bodies often, far ones
// rarely), when his build drops the body or changes its arrival, when the zoom takes it past a pixel
// or back, and, all of them, when he stops: standing still they are exact. The cover builds the due
// bodies under a budget a frame, the longest due first. The bands are few and hold the sim's shadow
// side to limit 8: they are rebuilt on every frame he walks or a new build arrives. Drawn only, never
// read by the sim.

#include "CoverPieces.h"

#include <ecs/systems/VisionSystem.h>

#include <vision/SightTunables.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace worldview {

	class DrawnShadows {
	  public:
		// One shadow edge as drawn this frame.
		struct EdgeDrawn {
			std::uint32_t body = 0;	  // into the build's bodies
			std::uint64_t bodyId = 0; // its serial or wall body, and its diameter
			std::int64_t  diameterMm = 0;
			std::uint32_t buildCorner = 0;
			std::uint32_t followedCorner = 0;
			ShadowBand	  band;
			double		  ease = 1.0; // how far the apex has come from its old corner to the followed one
		};
		// One soft body's pieces as last built, and since which frame they have been due (0: not due).
		struct SoftDrawn {
			std::uint64_t			   bodyId = 0;
			std::int64_t			   diameterMm = 0;
			SoftShadow				   shadow;
			bool					   drawn = false; // built with pieces: in reach, outside his hull, and a pixel or more
			bool					   shows = false; // whether it looked able to draw when it was built (softBodyShows)
			std::size_t				   ring = 0;	  // which of kSoftRings its pieces are kept in
			glm::dvec2				   builtFrom{0.0};
			std::int64_t			   builtArrival = 0;
			std::uint64_t			   dueSince = 0;
			std::vector<ValueTriangle> pieces;
		};
		// What the last frame did.
		struct Counts {
			std::size_t	  softBuilt = 0;		// soft bodies built this frame
			std::uint32_t softChangedRings = 0; // one bit a ring whose pieces changed: a body built or dropped
			bool		  bandsRebuilt = false;
		};

		// The frame's first half, with him at `eyeMm` having walked `walkedMm` since the last, over `bodies`
		// of the build `buildRevision`: drops the bodies the build no longer holds, marks the due ones with
		// `frame`, and moves the bands on. `bright` is his bright outline; bodies farther than the least of
		// drawnShadowReachMm, the build's reach, and `brightReachMm` draw nothing, every piece being clipped
		// to that outline, and nor do bodies under a pixel at `pixelsPerMm`.
		void update(
			const ecs::VisionSystem::SightBodies& bodies,
			std::uint64_t						  buildRevision,
			glm::dvec2							  eyeMm,
			double								  walkedMm,
			std::span<const geometry::Vec2i64>	  bright,
			double								  brightReachMm,
			double								  pixelsPerMm,
			const vision::SightTunables&		  tunables,
			std::uint64_t						  frame
		);

		// A due soft body: since when, its key, and the pieces it last took (an estimate of its cost).
		struct Due {
			std::uint64_t since = 0;
			std::uint64_t key = 0;
			std::size_t	  pieces = 0;
		};
		void appendDue(std::vector<Due>& out) const;
		// The frame's second half: builds the due body `key` from where he stands now.
		// The pieces it built.
		std::size_t build(std::uint64_t key);

		// The soft bodies are kept in rings by distance, so a near body, rebuilt often, does not send the far
		// ones up again with it. Every soft body's pieces in `ring`, in one list.
		static constexpr std::size_t					kSoftRings = 4;
		void											appendSoftPieces(std::size_t ring, std::vector<ValueTriangle>& out) const;
		[[nodiscard]] const std::vector<ValueTriangle>& bandPieces() const { return bandTriangles; }
		[[nodiscard]] const std::vector<EdgeDrawn>&		edges() const { return edgesDrawn; }
		[[nodiscard]] const std::unordered_map<std::uint64_t, SoftDrawn>& softBodies() const { return soft; }
		[[nodiscard]] std::size_t										  softPieceCount() const;
		[[nodiscard]] const Counts&										  counts() const { return frameCounts; }

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
			bool					  started = false;
		};

		static std::uint64_t keyOf(const vision::SightBody& body);
		// The build's shadow edges take up the running eases of the last build's (3.1).
		void takeUpEases(const ecs::VisionSystem::SightBodies& bodies);
		// Moves each apex on and rebuilds the bands; false when nothing they draw has changed.
		bool buildBands(const ecs::VisionSystem::SightBodies& bodies, double reachLimit, bool force);

		// This frame's inputs, kept for build().
		ecs::VisionSystem::SightBodies	   frameBodies;
		glm::dvec2						   frameEye{0.0};
		std::span<const geometry::Vec2i64> frameBright;
		double							   frameReachLimit = 0.0;
		double							   framePixelsPerMm = 0.0;
		vision::SightTunables			   frameTunables;
		double							   frameWalkedMm = 0.0;

		std::uint64_t								   lastBuild = 0;
		bool										   built = false;
		bool										   walking = false;
		double										   lookedPixelsPerMm = 0.0;
		double										   lookedReachLimit = 0.0;
		std::vector<CornerEase>						   eases;	  // one a shadow edge of the build, in its order
		std::unordered_map<std::uint64_t, SoftDrawn>   soft;	  // by body key
		std::unordered_map<std::uint64_t, std::size_t> bodyIndex; // body key to its index in the build's bodies
		vision::SightTunables						   softTunables;
		bool										   bandsBuilt = false;
		glm::dvec2									   bandEye{0.0};
		vision::SightTunables						   bandTunables;
		std::vector<ValueTriangle>					   bandTriangles;
		std::vector<EdgeDrawn>						   edgesDrawn;
		Counts										   frameCounts;
	};

} // namespace worldview
