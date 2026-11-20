#pragma once

// One colonist's drawn shadows (sight-softening.md 3.3, 7.3), in world millimetres, over the bodies of
// his last build: a triangle and two fins per soft body, and a band and a fin per shadow edge. The
// band's apex eases along a body face over shadowCornerEaseMm of his walk when the corner it is
// followed to changes (3.1, the corner switch).
//
// The soft bodies are kept in kSoftRings rings by distance from him, each built whole from where he
// stands and uploaded as one mesh set. A ring falls due when he has walked softShadowRebuildPercent of
// its nearest distance since it was built, so near rings often and far ones rarely, and every byte a
// ring uploads is a body it rebuilt; when his build drops, changes, or adds near him a body it holds,
// or changes one's arrival while he stands; when the zoom or the reach changes; and once he has stood
// still a few frames, so standing every triangle is exact. Neighbouring rings overlap, so a body
// crossing between them is in one or both and never in neither. The cover builds the due rings under
// a budget a frame, the most turned first, waiting counted as turn. The bands are few and are rebuilt on every frame he walks
// or a new build arrives. Drawn only, never read by the sim.

#include "CoverPieces.h"

#include <ecs/systems/VisionSystem.h>

#include <vision/SightTunables.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace worldview {

	class DrawnShadows {
	  public:
		static constexpr std::size_t kSoftRings = 4;
		// Where each ring starts, in millimetres from him; the last holds the rest of his reach.
		static constexpr std::array<double, kSoftRings> kSoftRingStartMm = {0.0, 4000.0, 8000.0, 16000.0};
		// How far each ring reaches past its own band either side, as a share of the band's edge.
		static constexpr double kSoftRingOverlap = 0.1;
		// A ring's cadence never reads a distance under this, so the nearest ring is not rebuilt every
		// frame for a trunk he brushes past.
		static constexpr double kSoftRingLeastDistanceMm = 1000.0;
		// He has stopped once he has stayed within kStillMm of where a window began for kStillSeconds; moving
		// past it starts the window again. So a frame or two without a step is not a stop, and nor is any
		// walk faster than kStillMm / kStillSeconds (10 mm/s), at any frame rate.
		static constexpr double kStillMm = 1.0;
		static constexpr double kStillSeconds = 0.1;

		// One shadow edge as drawn this frame.
		struct EdgeDrawn {
			std::uint32_t body = 0; // into the build's bodies
			std::uint64_t bodyId = 0;
			std::int64_t  diameterMm = 0;
			std::uint32_t buildCorner = 0;
			std::uint32_t followedCorner = 0;
			ShadowBand	  band;
			double		  ease = 1.0; // how far the apex has come from its old corner to the followed one
		};
		// One soft body as its ring last built it.
		struct SoftDrawn {
			std::uint64_t bodyId = 0;
			std::int64_t  diameterMm = 0;
			SoftShadow	  shadow;
			std::size_t	  pieces = 0;
			bool		  home = true; // in this ring's own band, not only its overlap with a neighbour's
		};
		// A body a ring was built over, to tell when the build changes it.
		struct Member {
			std::uint64_t key = 0;
			std::uint64_t outline = 0; // a hash of its outline
			std::int64_t  arrival = 0;
		};
		struct Ring {
			std::vector<ValueTriangle>		  pieces;
			std::vector<SoftDrawn>			  drawn;
			std::vector<Member>				  members;
			std::unordered_set<std::uint64_t> memberKeys;
			glm::dvec2						  builtFrom{0.0};
			bool							  built = false;
			std::uint64_t					  dueSince = 0; // the frame it fell due, 0 while not due
		};
		// What the last frame did.
		struct Counts {
			std::size_t	  ringsBuilt = 0;
			std::size_t	  bodiesBuilt = 0;
			std::uint32_t softChangedRings = 0; // one bit a ring whose pieces changed
			bool		  bandsRebuilt = false;
		};

		// The frame's first half, `seconds` after the last, with him at `eyeMm` having walked `walkedMm` since, over `bodies`
		// of the build `buildRevision`: marks the due rings with `frame` and moves the bands on. `bright`
		// is his bright outline; bodies farther than the least of drawnShadowReachMm, the build's reach,
		// and `brightReachMm` draw nothing, every piece being clipped to that outline, and nor do bodies
		// under a pixel at `pixelsPerMm`.
		void update(
			const ecs::VisionSystem::SightBodies& bodies,
			std::uint64_t						  buildRevision,
			glm::dvec2							  eyeMm,
			double								  walkedMm,
			double								  seconds,
			std::span<const geometry::Vec2i64>	  bright,
			double								  brightReachMm,
			double								  pixelsPerMm,
			const vision::SightTunables&		  tunables,
			std::uint64_t						  frame
		);

		// A due ring: since when, how far it has turned (his walk since it was built over its nearest
		// distance, the sine of the turn), which, and the pieces it took when last built (its cost).
		struct Due {
			std::uint64_t since = 0;
			double		  turn = 0.0;
			std::size_t	  ring = 0;
			std::size_t	  pieces = 0;
		};
		void appendDue(std::vector<Due>& out) const;
		// The frame's second half: builds `ring` from where he stands now; the pieces it built.
		std::size_t build(std::size_t ring);

		// How far a soft body is from `eyeMm` for its ring and its reach: its centre's distance less half
		// its diameter.
		[[nodiscard]] static double distanceOf(const vision::SightBody& body, glm::dvec2 eyeMm);

		[[nodiscard]] const std::array<Ring, kSoftRings>& rings() const { return softRings; }
		[[nodiscard]] const std::vector<ValueTriangle>&	  bandPieces() const { return bandTriangles; }
		[[nodiscard]] const std::vector<EdgeDrawn>&		  edges() const { return edgesDrawn; }
		[[nodiscard]] std::size_t						  softPieceCount() const;
		[[nodiscard]] std::size_t						  softBodyCount() const; // each body once, in its own band's ring
		[[nodiscard]] const Counts&						  counts() const { return frameCounts; }

	  private:
		// One shadow edge's apex: where it is drawn, where its ease started, the corner it eases to, and
		// his walk since that corner was taken.
		struct CornerEase {
			vision::SightBody::Source source = vision::SightBody::Source::Placed;
			std::uint64_t			  outline = 0; // a hash of its body's outline
			int						  litTurn = 1;
			geometry::Vec2i64		  buildCorner{0, 0};
			glm::dvec2				  apex{0.0};
			glm::dvec2				  from{0.0};
			glm::dvec2				  target{0.0};
			double					  walkedMm = 0.0;
			bool					  started = false;
		};

		static std::uint64_t keyOf(const vision::SightBody& body);
		static std::uint64_t outlineOf(const vision::SightBody& body);
		// The ring bands a body at `distanceMm` lies in, one bit a ring.
		static std::uint32_t ringsAt(double distanceMm);
		// The nearest distance a ring's cadence and turn read.
		static double nearestOf(std::size_t ring);
		// The build's shadow edges take up the running eases of the last build's (3.1).
		void takeUpEases(const ecs::VisionSystem::SightBodies& bodies);
		// Moves each apex on and rebuilds the bands; false when nothing they draw has changed.
		bool buildBands(const ecs::VisionSystem::SightBodies& bodies, double reachLimit, bool force);
		// Which rings a new build changes: a member dropped or reshaped, an arrival changed while he
		// stands, or a body new to his build that already shows inside his bright shape.
		std::uint32_t ringsChangedBy(const ecs::VisionSystem::SightBodies& bodies);

		// This frame's inputs, kept for build().
		ecs::VisionSystem::SightBodies	   frameBodies;
		glm::dvec2						   frameEye{0.0};
		std::span<const geometry::Vec2i64> frameBright;
		double							   frameReachLimit = 0.0;
		double							   framePixelsPerMm = 0.0;
		vision::SightTunables			   frameTunables;
		double							   frameWalkedMm = 0.0;

		std::uint64_t									 lastBuild = 0;
		bool											 built = false;
		glm::dvec2										 stillAt{0.0};
		double											 stillSeconds = 0.0;
		bool											 stopBuilt = false;
		double											 lookedPixelsPerMm = 0.0;
		double											 lookedReachLimit = 0.0;
		vision::SightTunables							 softTunables;
		std::vector<CornerEase>							 eases; // one a shadow edge of the build, in its order
		std::array<Ring, kSoftRings>					 softRings;
		std::unordered_map<std::uint64_t, std::uint64_t> knownOutlines; // the last build's soft bodies, key to outline
		std::unordered_map<std::uint64_t, std::uint64_t> buildOutlines; // this build's, reused
		std::unordered_set<std::uint64_t>				 heldKeys;		// a ring's members before its build, reused
		bool											 bandsBuilt = false;
		glm::dvec2										 bandEye{0.0};
		vision::SightTunables							 bandTunables;
		std::vector<ValueTriangle>						 bandTriangles;
		std::vector<EdgeDrawn>							 edgesDrawn;
		Counts											 frameCounts;
	};

} // namespace worldview
