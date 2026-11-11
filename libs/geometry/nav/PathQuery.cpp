#include "PathQuery.h"

#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"
#include "NavMesh.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace geometry::nav {

	namespace {

		struct Vec2d {
			double x = 0.0;
			double y = 0.0;
		};

		inline Vec2d toD(const Vec2i64& p) {
			return {static_cast<double>(p.x), static_cast<double>(p.y)};
		}

		inline Vec2d operator+(const Vec2d& a, const Vec2d& b) {
			return {a.x + b.x, a.y + b.y};
		}
		inline Vec2d operator-(const Vec2d& a, const Vec2d& b) {
			return {a.x - b.x, a.y - b.y};
		}
		inline Vec2d operator*(const Vec2d& a, double s) {
			return {a.x * s, a.y * s};
		}

		inline bool same(const Vec2d& a, const Vec2d& b) {
			return a.x == b.x && a.y == b.y;
		}

		inline double lengthD(const Vec2d& a) {
			return std::sqrt(a.x * a.x + a.y * a.y);
		}

		inline double distanceD(const Vec2d& a, const Vec2d& b) {
			return lengthD(b - a);
		}

		// Signed doubled area of triangle (a, b, c); >0 when CCW. Matches the
		// orientation predicate's sign convention but in double for the funnel's
		// shape math only.
		inline double area2(const Vec2d& a, const Vec2d& b, const Vec2d& c) {
			return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
		}

		inline double distanceToSegment(const Vec2d& p, const Vec2d& a, const Vec2d& b) {
			const Vec2d	 d	  = b - a;
			const double len2 = d.x * d.x + d.y * d.y;
			if (len2 <= 0.0) {
				return distanceD(p, a);
			}
			const double u = std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2, 0.0, 1.0);
			return distanceD(p, a + d * u);
		}

		inline Vec2i64 cornerSum(const NavView& mesh, const NavTriangle& t) {
			return mesh.vertex(t.v[0]) + mesh.vertex(t.v[1]) + mesh.vertex(t.v[2]);
		}

		inline Vec2i64 edgeSum(const NavView& mesh, const NavTriangle& t, int e) {
			return mesh.vertex(t.v[e]) + mesh.vertex(t.v[(e + 1) % 3]);
		}

		// Locate which edge of `from` faces neighbor `to`.
		inline int sharedEdgeIndex(const NavTriangle& from, std::int32_t to) {
			for (int e = 0; e < 3; ++e) {
				if (from.neighbor[e] == to) {
					return e;
				}
			}
			return -1;
		}

		// The apex (local vertex index) shared by two edges of a triangle: the vertex
		// common to edge e1 = (v[e1],v[e1+1]) and e2 = (v[e2],v[e2+1]). For two distinct
		// edges of a triangle exactly one vertex is shared; that vertex keys the Demyen
		// edge-pair width for the passage that enters one edge and leaves the other.
		inline int apexVertexOfEdgePair(int e1, int e2) {
			const int a0 = e1, a1 = (e1 + 1) % 3;
			const int b0 = e2, b1 = (e2 + 1) % 3;
			if (a0 == b0 || a0 == b1) {
				return a0;
			}
			return a1; // a1 must be the shared vertex (distinct edges share one vertex)
		}

		// Does triangle `t` admit a disc of diameter `2*radius` passing between the edges
		// meeting at apex `apexLocal`? Demyen edge-pair width vs common-knowledge
		// obstacles. kUnconstrainedWidth always admits. Exact: width is the floored mm
		// clearance, and floor(width) >= 2r iff width >= 2r for the integer threshold.
		inline bool widthAdmits(const NavTriangle& t, int apexLocal, std::int64_t diameterMm) {
			return t.edgePairWidthMm[apexLocal] >= diameterMm;
		}

		Vec2i64 roundToMm(const Vec2d& p) {
			return {static_cast<std::int64_t>(std::llround(p.x)), static_cast<std::int64_t>(std::llround(p.y))};
		}

		// Is triangle `t` crossable for this belief? The truth-vs-belief predicate from
		// pathfinding-architecture section 5; the door-span trick (a pathable door's
		// footprint is a blocked face carrying faceOpening) is what makes a truth query
		// reproduce v1: solid wall blocks, door passes.
		inline bool freelyTraversable(const NavTriangle& t, const BeliefFilter& belief) {
			if (isFloorFace(t)) {
				return true; // real floor
			}
			if (isCommonKnowledgeTerrainFace(t)) {
				return false; // negative sentinel: common-knowledge terrain (or a junction
							   // with no incident-wall id) -- always blocks, filter or not
			}
			// faceBlocker > 0: a wall segment id (a junction tagged with an incident wall
			// lands here too, so it is belief-gated like that wall).
			const std::uint64_t segment = static_cast<std::uint64_t>(t.faceBlocker);
			if (belief.knownSegments == nullptr) {
				// TRUTH: the wall is known solid unless this face is a door span.
				return t.faceOpening != kNoOpening;
			}
			// BELIEF: an unseen wall is absent (walkable); a seen wall blocks unless the
			// agent also knows a door through it on this very face.
			if (belief.knownSegments->count(segment) == 0) {
				return true;
			}
			return t.faceOpening != kNoOpening && belief.knownOpenings != nullptr &&
				   belief.knownOpenings->count(static_cast<std::uint64_t>(t.faceOpening)) != 0;
		}

		// What walking a length inside triangle t costs per mm for this query: 1 on ground
		// it crosses freely, its tag's multiplier (above 1) for a face this filter would
		// block but prices, 0 for a face it may not cross at all.
		inline double stepMultiplier(const NavView& mesh, std::int32_t t, const BeliefFilter& belief) {
			const NavTriangle& tri = mesh.triangle(t);
			if (freelyTraversable(tri, belief)) {
				return 1.0;
			}
			for (const TagPrice& price : belief.prices) {
				if (price.tag == tri.faceBlocker) {
					return price.multiplier;
				}
			}
			return 0.0;
		}

		inline bool traversable(const NavView& mesh, std::int32_t t, const BeliefFilter& belief) {
			return stepMultiplier(mesh, t, belief) > 0.0;
		}

		// The reachability forest the belief selects: truth (AI goal validity) when the
		// agent knows everything, else terrain (the sound reject for every belief). A
		// belief query never uses the truth forest, because an agent optimistically
		// routing through unseen walls can reach triangles the truth forest walls off.
		const ReachabilityForest* forestFor(const NavView& mesh, const BeliefFilter& belief) {
			return mesh.forest(belief.knownSegments == nullptr);
		}

		// The triangle a walker stands in at p, located as `tri`: tri itself when this
		// belief crosses it freely, else, for a p on an edge or corner of tri, the triangle
		// holding p it crosses freely whose sorted corners come first, then one it crosses
		// at a price, then tri. Point location settles a shared edge or corner by geometry
		// alone, so a start on a tree's or wall's edge would otherwise land in the blocked
		// face and find no path, or in the priced face and be charged for standing beside
		// it: only a point strictly inside a priced face stands on it.
		std::int32_t standOn(const NavView& mesh, std::int32_t tri, const Vec2i64& p, const BeliefFilter& belief) {
			if (tri < 0 || stepMultiplier(mesh, tri, belief) == 1.0) {
				return tri;
			}
			std::int32_t		   best		= -1;
			bool				   bestFree = false;
			std::array<Vec2i64, 3> bestCorners;
			auto				   consider = [&](std::int32_t u) {
				  if (u < 0) {
					  return;
				  }
				  const double m = stepMultiplier(mesh, u, belief);
				  if (m <= 0.0) {
					  return;
				  }
				  const NavTriangle&	 t = mesh.triangle(u);
				  std::array<Vec2i64, 3> corners{mesh.vertex(t.v[0]), mesh.vertex(t.v[1]), mesh.vertex(t.v[2])};
				  std::sort(corners.begin(), corners.end());
				  const bool free = m == 1.0;
				  if (best < 0 || (free && !bestFree) || (free == bestFree && corners < bestCorners)) {
					  best		  = u;
					  bestFree	  = free;
					  bestCorners = corners;
				  }
			};
			const NavTriangle& t = mesh.triangle(tri);
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& a = mesh.vertex(t.v[e]);
				const Vec2i64& b = mesh.vertex(t.v[(e + 1) % 3]);
				if (p == a) {
					// A corner: every triangle round it holds p.
					std::int32_t cur = tri;
					int			 li	 = e;
					for (int guard = 0; guard < 256; ++guard) {
						consider(cur);
						const std::int32_t nb = mesh.triangle(cur).neighbor[(li + 2) % 3];
						if (nb < 0 || nb == tri) {
							break;
						}
						const NavTriangle& next = mesh.triangle(nb);
						li						= next.v[0] == t.v[e] ? 0 : (next.v[1] == t.v[e] ? 1 : 2);
						cur						= nb;
					}
					cur = tri;
					li	= e;
					for (int guard = 0; guard < 256; ++guard) {
						const std::int32_t nb = mesh.triangle(cur).neighbor[li];
						if (nb < 0 || nb == tri) {
							break;
						}
						const NavTriangle& next = mesh.triangle(nb);
						li						= next.v[0] == t.v[e] ? 0 : (next.v[1] == t.v[e] ? 1 : 2);
						cur						= nb;
						consider(cur);
					}
				} else if (orientation(a, b, p) == Orientation::Collinear) {
					consider(t.neighbor[e]);
				}
			}
			if (best >= 0 && (bestFree || !traversable(mesh, tri, belief))) {
				return best;
			}
			return tri;
		}

		// pathThrough()'s sound short-circuit, the same test reaches() makes. Both
		// triangles are already located and known traversable. Returns true when the
		// forest PROVES no disc of `diameterMm` can get from startTri to goalTri:
		// different component (any diameter), or the path bottleneck < the diameter.
		bool reachabilityRejects(
			const NavView& mesh, std::int32_t startTri, std::int32_t goalTri, std::int64_t diameterMm,
			const BeliefFilter& belief) {
			const ReachabilityForest* forest = forestFor(mesh, belief);
			return forest != nullptr && !inForestSubtree(*forest, reachRootInForest(*forest, startTri, diameterMm), goalTri);
		}

		[[nodiscard]] std::int64_t discDiameterMm(std::int64_t agentRadiusMm) {
			return (agentRadiusMm > 0) ? agentRadiusMm * 2 : 0;
		}

		// Collapse consecutive duplicate points and points collinear with their
		// neighbours (the middle of three collinear points carries no information).
		void appendTaut(std::vector<Vec2i64>& out, const Vec2i64& p) {
			if (!out.empty() && out.back() == p) {
				return;
			}
			if (out.size() >= 2) {
				const Vec2i64& a = out[out.size() - 2];
				const Vec2i64& b = out[out.size() - 1];
				if (orientation(a, b, p) == Orientation::Collinear) {
					// b is redundant: a, b, p are collinear; the funnel advances
					// monotonically, so p extends past b along the same ray.
					out.back() = p;
					return;
				}
			}
			out.push_back(p);
		}

		// Does a disc need to keep its radius from corner i of triangle t? Only if some
		// triangle around that vertex is one this query cannot cross freely (blocked, or
		// priced), or the vertex is on the mesh boundary. A vertex in open ground (a seam's
		// split vertex, a free diagonal's end) is no obstacle, and pulling a portal in from it
		// would bend paths at nothing. With `hardOnly`, priced faces do not count: a disc
		// walking through priced ground keeps clear only of what it cannot cross.
		bool isCorner(const NavView& mesh, std::int32_t t, int i, const BeliefFilter& belief, bool hardOnly) {
			std::int32_t cur = t;
			int			 li	 = i;
			for (int guard = 0; guard < 256; ++guard) {
				const double m = stepMultiplier(mesh, cur, belief);
				if (hardOnly ? m <= 0.0 : m != 1.0) {
					return true;
				}
				const NavTriangle&	tri = mesh.triangle(cur);
				const std::uint32_t v	= tri.v[li];
				const std::int32_t	nb = tri.neighbor[(li + 2) % 3]; // across the edge ending at v
				if (nb < 0) {
					return true;
				}
				if (nb == t) {
					return false;
				}
				const NavTriangle& next = mesh.triangle(nb);
				li						= next.v[0] == v ? 0 : (next.v[1] == v ? 1 : 2);
				cur						= nb;
			}
			return true;
		}

		// Edge e of triangle t as a portal, in the string-pull's terms: the edge runs
		// v[e] -> v[e+1] with t's interior on its left, so walking out through it the
		// tail is `left` and the head `right` under the funnel's sign convention. An end
		// a disc must keep clear of is pulled in along the edge (see portalSpan), so the
		// taut path keeps that clearance from the walls.
		//
		// CLEARANCE CAVEAT: this per-endpoint inset along the portal edge approximates
		// the true radius-offset (Minkowski) shortest path. When a leg rounds an obstacle
		// vertex at an oblique angle, the emitted polyline can under-clear that vertex by
		// a few percent of the radius. The routing DECISION is sound (the width gates
		// never route a disc through a gap narrower than its diameter); only the emitted
		// geometry is approximate near oblique corners.
		struct PortalSpan {
			Vec2d left;
			Vec2d right;
		};

		// What a vertex's star holds, for isCorner: nothing a walker can be stopped by
		// (open ground, never a corner), common-knowledge terrain (a corner for every
		// query: blocked, or priced and so still an obstacle to keep clear of), the mesh
		// boundary (a corner for every query), or walls and doors (a corner or not by
		// belief, so walked per query).
		enum class Star : std::uint8_t { Unknown, Open, Boundary, Obstacle, Walls };

		// A vertex's Star, and for an Obstacle the terrain tags round it, up to two (a third
		// marks more): whether the corner is hard for a query, a corner even with its priced
		// faces open, is then whether a tag is one the query does not price.
		struct StarInfo {
			Star						star = Star::Unknown;
			std::uint8_t				tagCount = 0;
			std::array<std::int64_t, 2> tags{};
		};

		StarInfo starOf(const NavView& mesh, std::int32_t t, int i) {
			std::int32_t cur	= t;
			int			 li		= i;
			bool		 walls	= false;
			StarInfo	 out;
			for (int guard = 0; guard < 256; ++guard) {
				const NavTriangle& tri = mesh.triangle(cur);
				if (isCommonKnowledgeTerrainFace(tri)) {
					const bool seen = (out.tagCount > 0 && out.tags[0] == tri.faceBlocker) || (out.tagCount > 1 && out.tags[1] == tri.faceBlocker);
					if (!seen && out.tagCount < 3) {
						if (out.tagCount < 2) {
							out.tags[out.tagCount] = tri.faceBlocker;
						}
						++out.tagCount;
					}
				}
				walls				   = walls || (!isFloorFace(tri) && !isCommonKnowledgeTerrainFace(tri));
				const std::uint32_t v  = tri.v[li];
				const std::int32_t	nb = tri.neighbor[(li + 2) % 3]; // across the edge ending at v
				if (nb < 0) {
					out.star = Star::Boundary;
					return out;
				}
				if (nb == t) {
					out.star = walls ? Star::Walls : (out.tagCount > 0 ? Star::Obstacle : Star::Open);
					return out;
				}
				const NavTriangle& next = mesh.triangle(nb);
				li						= next.v[0] == v ? 0 : (next.v[1] == v ? 1 : 2);
				cur						= nb;
			}
			out.star = Star::Boundary;
			return out;
		}

		// Each vertex's Star, walked once per mesh state and kept between queries on
		// this thread. A mesh state is known by its owner and its epoch, which no other
		// state in the process has had (nextMeshGeneration), so a mesh rebuilt where an
		// old one stood never reads the old one's stars. A new state costs a stamp, not a
		// pass over the vertices: a star is this state's when its stamp is.
		struct CornerMemo {
			const void*				   owner = nullptr;
			std::uint64_t			   epoch = 0;
			ScratchArray<StarInfo>		star;
			ScratchArray<std::uint32_t> starStamp;
			std::uint32_t			   state = 0;
			// Round walls a corner depends on the query's belief, and whether one is hard
			// (a corner without its priced faces) on its prices, so those answers last one
			// query: stamped with the query, 1 a corner, 0 not.
			ScratchArray<std::uint32_t> queryStamp;
			ScratchArray<char>			queryCorner;
			ScratchArray<std::uint32_t> hardStamp;
			ScratchArray<char>			queryHard;
			std::uint32_t			   query = 0;

			void begin(const NavView& mesh) {
				queryStamp.growTo(mesh.vertices.size());
				queryCorner.growTo(mesh.vertices.size());
				hardStamp.growTo(mesh.vertices.size());
				queryHard.growTo(mesh.vertices.size());
				if (++query == 0) {
					queryStamp.zero();
					hardStamp.zero();
					query = 1;
				}
				star.growTo(mesh.vertices.size());
				starStamp.growTo(mesh.vertices.size());
				if (owner != mesh.owner || epoch != mesh.epoch()) {
					owner = mesh.owner;
					epoch = mesh.epoch();
					if (++state == 0) {
						starStamp.zero();
						state = 1;
					}
				}
			}

			bool holds(const NavView& mesh) const { return owner == mesh.owner && epoch == mesh.epoch(); }

			const StarInfo& starAt(const NavView& mesh, std::int32_t t, int i) {
				const std::uint32_t v = mesh.triangle(t).v[i];
				if (starStamp[v] != state) {
					starStamp[v] = state;
					star[v]		 = starOf(mesh, t, i);
				}
				return star[v];
			}

			bool at(const NavView& mesh, std::int32_t t, int i, const BeliefFilter& belief) {
				const std::uint32_t v = mesh.triangle(t).v[i];
				const Star			s = starAt(mesh, t, i).star;
				if (s != Star::Walls) {
					return s != Star::Open;
				}
				if (queryStamp[v] != query) {
					queryStamp[v]  = query;
					queryCorner[v] = isCorner(mesh, t, i, belief, false) ? 1 : 0;
				}
				return queryCorner[v] != 0;
			}

			bool hardAt(const NavView& mesh, std::int32_t t, int i, const BeliefFilter& belief) {
				const std::uint32_t v	 = mesh.triangle(t).v[i];
				const StarInfo&		info = starAt(mesh, t, i);
				const Star			s	 = info.star;
				if (s == Star::Open || s == Star::Boundary) {
					return s == Star::Boundary;
				}
				if (s == Star::Obstacle && info.tagCount <= 2) {
					for (std::uint8_t k = 0; k < info.tagCount; ++k) {
						const auto priced = std::find_if(belief.prices.begin(), belief.prices.end(), [&](const TagPrice& p) { return p.tag == info.tags[k]; });
						if (priced == belief.prices.end()) {
							return true;
						}
					}
					return false;
				}
				if (hardStamp[v] != query) {
					hardStamp[v] = query;
					queryHard[v] = isCorner(mesh, t, i, belief, true) ? 1 : 0;
				}
				return queryHard[v] != 0;
			}
		};

		// Memos for two mesh states, so a thread that alternates two meshes (the game's
		// structural and nav meshes) keeps both: the state asked for, else the one used
		// longer ago gives way.
		struct CornerMemos {
			std::array<CornerMemo, 2>	 slots;
			std::array<std::uint64_t, 2> used{};
			std::uint64_t				 clock = 0;

			CornerMemo& begin(const NavView& mesh) {
				std::size_t pick = slots[1].holds(mesh) ? 1 : 0;
				if (!slots[pick].holds(mesh)) {
					pick = used[0] <= used[1] ? 0 : 1;
				}
				used[pick] = ++clock;
				slots[pick].begin(mesh);
				return slots[pick];
			}
		};

		// Edge e of triangle t as a portal, each end a disc must keep clear of pulled in
		// along the edge by the radius. With both ends pulled in, each moves at most half the
		// edge (the width gates have judged that the disc fits); with one, it may move the
		// whole edge, so a short edge from a corner still keeps the disc its radius away.
		// A portal into or out of priced ground keeps clear only of hard corners: the disc is
		// walking through that ground, so its own corners are no obstacle to it.
		inline PortalSpan portalSpan(const NavView& mesh, std::int32_t t, int e, double radius, const BeliefFilter& belief, CornerMemo& corners) {
			const NavTriangle& tri = mesh.triangle(t);
			PortalSpan		   span{toD(mesh.vertex(tri.v[e])), toD(mesh.vertex(tri.v[(e + 1) % 3]))};
			if (radius <= 0.0) {
				return span;
			}
			const Vec2d	 dir = span.right - span.left;
			const double len = lengthD(dir);
			if (len <= 0.0) {
				return span;
			}
			const Vec2d	 unit	   = dir * (1.0 / len);
			const bool	 throughPriced = stepMultiplier(mesh, t, belief) > 1.0 || stepMultiplier(mesh, tri.neighbor[e], belief) > 1.0;
			auto		 cornerAt	   = [&](int i) { return throughPriced ? corners.hardAt(mesh, t, i, belief) : corners.at(mesh, t, i, belief); };
			const bool	 leftEnd	   = cornerAt(e);
			const bool	 rightEnd	   = cornerAt((e + 1) % 3);
			const double off	   = std::min(radius, leftEnd && rightEnd ? len * 0.5 : len);
			const Vec2d	 tail	   = span.left;
			if (leftEnd) {
				span.left = tail + unit * off;
			}
			if (rightEnd) {
				span.right = span.right - unit * off;
			}
			return span;
		}

		// A portal as the string-pull reads it, with the search label that enters
		// through it (-1 where none does: the final string-pull).
		struct Portal {
			Vec2d		 left;
			Vec2d		 right;
			std::int32_t label = -1;
		};

		// A corridor's funnel: the last corner its taut path bends at (apex), the path
		// length to it, and the cone of directions from the apex that pass every portal
		// since, bounded by `left` and `right`. A bound equal to the apex is open. Each
		// point records the label whose portal it came from, so a bend can look back.
		struct Funnel {
			Vec2d		 apex;
			Vec2d		 left;
			Vec2d		 right;
			double		 length	   = 0.0;
			std::int32_t apexFrom  = -1;
			std::int32_t leftFrom  = -1;
			std::int32_t rightFrom = -1;
		};

		inline Funnel funnelAt(const Vec2d& p, std::int32_t state) {
			return {p, p, p, 0.0, state, state, state};
		}

		// One portal into a funnel that needs no bend for it: narrow the bounds and say so,
		// or leave the funnel as it was and say a bend is due.
		inline bool narrow(Funnel& f, const Portal& p) {
			const bool rightTightens = area2(f.apex, f.right, p.right) <= 0.0;
			if (rightTightens && !same(f.apex, f.right) && area2(f.apex, f.left, p.right) <= 0.0) {
				return false;
			}
			const bool leftTightens = area2(f.apex, f.left, p.left) >= 0.0;
			const Vec2d right	   = rightTightens ? p.right : f.right;
			if (leftTightens && !same(f.apex, f.left) && area2(f.apex, right, p.left) >= 0.0) {
				return false;
			}
			if (rightTightens) {
				f.right		= p.right;
				f.rightFrom = p.label;
			}
			if (leftTightens) {
				f.left	   = p.left;
				f.leftFrom = p.label;
			}
			return true;
		}

		// Mononen's "simple stupid funnel" string-pull of `portals` into `f`: each
		// portal narrows the cone, and one wholly past a bound bends the path at that
		// bound, which becomes the apex; the pull then resumes from the portal after the
		// one that bound came from. A bound from before `portals` resumes at the start,
		// so the cone after it is only as narrow as the portals given; the result says
		// whether that happened. `bends` (when given) collects the bend points.
		inline bool pull(Funnel& f, const std::vector<Portal>& portals, std::vector<Vec2i64>* bends) {
			bool		 lostHistory = false;
			std::int64_t leftIdx	 = -1;
			std::int64_t rightIdx	 = -1;
			auto		 bendAt		 = [&](const Vec2d& p, std::int32_t from, std::int64_t idx) {
				if (bends != nullptr) {
					appendTaut(*bends, roundToMm(p));
				}
				f.length += distanceD(f.apex, p);
				f.apex		= p;
				f.apexFrom	= from;
				f.left		= p;
				f.right		= p;
				f.leftFrom	= from;
				f.rightFrom = from;
				leftIdx		= idx;
				rightIdx	= idx;
				lostHistory = lostHistory || idx < 0;
				return idx + 1;
			};
			for (std::int64_t i = 0; i < static_cast<std::int64_t>(portals.size()); ++i) {
				const Portal& p = portals[static_cast<std::size_t>(i)];
				// Tighten the right bound.
				if (area2(f.apex, f.right, p.right) <= 0.0) {
					if (same(f.apex, f.right) || area2(f.apex, f.left, p.right) > 0.0) {
						f.right		= p.right;
						f.rightFrom = p.label;
						rightIdx	= i;
					} else {
						// Right crosses left: the left bound becomes the apex.
						i = bendAt(f.left, f.leftFrom, leftIdx) - 1;
						continue;
					}
				}
				// Tighten the left bound.
				if (area2(f.apex, f.left, p.left) >= 0.0) {
					if (same(f.apex, f.left) || area2(f.apex, f.right, p.left) < 0.0) {
						f.left	   = p.left;
						f.leftFrom = p.label;
						leftIdx	   = i;
					} else {
						// Left crosses right: the right bound becomes the apex.
						i = bendAt(f.right, f.rightFrom, rightIdx) - 1;
						continue;
					}
				}
			}
			return lostHistory;
		}

		// The point of segment [a, b] where the walk p -> point -> target is shortest: the
		// straight line's crossing when it crosses, else the nearer end. The walk's length
		// is convex along the segment, so this is its minimum; reflecting the target across
		// the segment's line covers a target on p's side.
		inline Vec2d shortestCrossing(const Vec2d& a, const Vec2d& b, const Vec2d& p, const Vec2d& target) {
			const Vec2d	 d	  = b - a;
			const double len2 = d.x * d.x + d.y * d.y;
			if (len2 <= 0.0) {
				return a;
			}
			const double sideP = area2(a, b, p);
			if (sideP == 0.0) {
				// p on the line: the nearest point of the segment to p is best.
				return a + d * std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2, 0.0, 1.0);
			}
			Vec2d aim = target;
			if ((sideP > 0.0) == (area2(a, b, target) > 0.0)) {
				const double k = 2.0 * ((target.x - a.x) * d.x + (target.y - a.y) * d.y) / len2;
				aim			   = a + d * k - (target - a);
			}
			const Vec2d	 w	   = aim - p;
			const double denom = d.x * w.y - d.y * w.x;
			double		 u	   = 0.5;
			if (denom != 0.0) {
				u = ((p.x - a.x) * w.y - (p.y - a.y) * w.x) / denom;
			}
			return a + d * std::clamp(u, 0.0, 1.0);
		}

		// A lower bound on the walk from the corridor's start to `target` that leaves the
		// corridor through `portal` (the portal the funnel was last pulled through). Every
		// such walk reaches the apex and crosses the portal: straight to the part the cone
		// sees, or round a bound to the part past it. So the least of those three, each to
		// its best crossing and then straight on, is no longer than any of them.
		inline double walkVia(const Funnel& f, const Portal& portal, const Vec2d& target) {
			// The common case first: the straight line from the apex to the target crosses
			// the portal where the cone sees it, and nothing is shorter than that line.
			const double sideApex	= area2(portal.left, portal.right, f.apex);
			const double sideTarget = area2(portal.left, portal.right, target);
			if ((sideApex > 0.0 && sideTarget < 0.0) || (sideApex < 0.0 && sideTarget > 0.0)) {
				const Vec2d x = f.apex + (target - f.apex) * (sideApex / (sideApex - sideTarget));
				if (area2(f.apex, target, portal.left) * area2(f.apex, target, portal.right) <= 0.0 &&
					(same(f.left, f.apex) || area2(f.apex, f.left, x) >= 0.0) && (same(f.right, f.apex) || area2(f.apex, f.right, x) <= 0.0)) {
					return f.length + distanceD(f.apex, target);
				}
			}
			const Vec2d d  = portal.right - portal.left;
			double		lo = 0.0;
			double		hi = 1.0;
			// Keep the points of the portal on the inner side of a bound: area2 against the
			// bound's ray is linear along the portal.
			auto clip = [&](const Vec2d& bound, double side) {
				if (same(bound, f.apex)) {
					return;
				}
				const double a0 = side * area2(f.apex, bound, portal.left);
				const double a1 = side * area2(f.apex, bound, portal.right);
				if (a0 >= 0.0 && a1 >= 0.0) {
					return;
				}
				if (a0 < 0.0 && a1 < 0.0) {
					lo = 1.0;
					hi = 0.0;
					return;
				}
				const double t = a0 / (a0 - a1);
				if (a0 < 0.0) {
					lo = std::max(lo, t);
				} else {
					hi = std::min(hi, t);
				}
			};
			clip(f.left, 1.0);
			clip(f.right, -1.0);
			if (lo > hi) {
				// Rounding left the cone just off the portal: the whole portal still bounds it.
				lo = 0.0;
				hi = 1.0;
			}
			auto via = [&](const Vec2d& from, double t0, double t1) {
				const Vec2d x = shortestCrossing(portal.left + d * t0, portal.left + d * t1, from, target);
				return distanceD(from, x) + distanceD(x, target);
			};
			double best = via(f.apex, lo, hi);
			if (lo > 0.0) {
				best = std::min(best, distanceD(f.apex, f.left) + via(f.left, 0.0, lo));
			}
			if (hi < 1.0) {
				best = std::min(best, distanceD(f.apex, f.right) + via(f.right, hi, 1.0));
			}
			return f.length + best;
		}

		// The corridor's taut length to p once p is pulled in as its last portal.
		double lengthTo(const Funnel& f, const Vec2d& p) {
			return f.length + distanceD(f.apex, p);
		}

		// A search label: one corridor's funnel reaching a state (triangle, entry edge),
		// with the portal it entered by and the label it came from.
		struct Label {
			Funnel		 funnel;
			Portal		 entry;
			double		 best	  = 0.0;
			std::int32_t state	  = -1;
			std::int32_t cameFrom = -1;
			bool		 closed	  = false;
			bool		 exact	  = false; // best is the corridor's taut length (or cost) to the goal
			bool		 priced	  = false; // the corridor crosses a face walked at a price
			double		 squeeze  = 1.0;   // the price of the step into this label's triangle (see the width gate)
			// A lower bound on what priced faces add to the corridor's cost: the runs it has
			// left, each (multiplier - 1) times the least distance between the portals it
			// came in and went out by, and the run it is in (entered by runEntry, at the
			// least multiplier runPrice so far; runPrice 0 outside one).
			double		 extra	  = 0.0;
			Portal		 runEntry;
			double		 runPrice = 0.0;
		};

		// A state holds up to kLabelsPerState corridors at once. Corridors that bend at
		// different corners can each be the better one depending on the portals still
		// to come, so one is dropped only when another is at least as short to every
		// point of the shared portal (its ends and middle), or, with the state full,
		// when it scores worst.
		constexpr int kLabelsPerState = 2;

		struct StateLabels {
			std::array<std::int32_t, kLabelsPerState> ids{};
			int										  count = 0;
		};

		// Labels and the states holding them, kept between queries on this thread so a
		// query costs what it expands, not the mesh's size. A state's entry is this
		// query's when its stamp is the current one.
		struct SearchScratch {
			std::vector<Label>		   labels;
			ScratchArray<StateLabels>	states;
			ScratchArray<std::uint32_t> stamp;
			std::uint32_t			   current = 0;
			std::vector<Portal>		   replay;
			std::vector<std::int32_t>  corridor; // a winning corridor's triangles
			std::vector<double>		   squeeze;	 // and the price of leaving each
			std::vector<Vec2i64>	   points;	 // a candidate corridor's string-pull
			CornerMemos				   corners;

			StateLabels& held(std::int32_t state) {
				const std::size_t s = static_cast<std::size_t>(state);
				if (stamp[s] != current) {
					stamp[s]		= current;
					states[s].count = 0;
				}
				return states[s];
			}
		};

		SearchScratch& searchScratch(std::size_t stateCount) {
			thread_local SearchScratch scratch;
			scratch.labels.clear();
			scratch.stamp.growTo(stateCount);
			scratch.states.growTo(stateCount);
			if (++scratch.current == 0) {
				scratch.stamp.zero();
				scratch.current = 1;
			}
			return scratch;
		}

		// A funnel's lengths to the sample points of a portal: its ends and middle.
		// The corridor's taut length to p, a point of the portal it was last pulled
		// through: straight when the cone sees p, else round the bound p lies past.
		inline double reachTo(const Funnel& f, const Vec2d& p) {
			if (!same(f.left, f.apex) && area2(f.apex, f.left, p) < 0.0) {
				return f.length + distanceD(f.apex, f.left) + distanceD(f.left, p);
			}
			if (!same(f.right, f.apex) && area2(f.apex, f.right, p) > 0.0) {
				return f.length + distanceD(f.apex, f.right) + distanceD(f.right, p);
			}
			return f.length + distanceD(f.apex, p);
		}

		inline std::array<double, 3> reachOf(const Funnel& f, const Portal& portal) {
			return {reachTo(f, portal.left), reachTo(f, portal.right), reachTo(f, (portal.left + portal.right) * 0.5)};
		}

		// Is a corridor reaching a portal with lengths `a` at least as short as one with `b`?
		inline bool noLonger(const std::array<double, 3>& a, const std::array<double, 3>& b) {
			return a[0] <= b[0] && a[1] <= b[1] && a[2] <= b[2];
		}

		// Mixes a coordinate into a geometric tie key.
		inline std::uint64_t mixKey(std::uint64_t key, std::uint64_t value) {
			key ^= value + 0x9E3779B97F4A7C15ULL + (key << 6) + (key >> 2);
			key ^= key >> 31;
			key *= 0xBF58476D1CE4E5B9ULL;
			return key ^ (key >> 27);
		}

		// The least distance between segments [a, b] and [p, q] (0 when they meet).
		double segmentToSegment(const Vec2d& a, const Vec2d& b, const Vec2d& p, const Vec2d& q) {
			const double ap = area2(a, b, p);
			const double aq = area2(a, b, q);
			const double pa = area2(p, q, a);
			const double pb = area2(p, q, b);
			if (((ap <= 0.0 && aq >= 0.0) || (ap >= 0.0 && aq <= 0.0)) && ((pa <= 0.0 && pb >= 0.0) || (pa >= 0.0 && pb <= 0.0))) {
				return 0.0;
			}
			return std::min({distanceToSegment(p, a, b), distanceToSegment(q, a, b), distanceToSegment(a, p, q), distanceToSegment(b, p, q)});
		}

	} // namespace

	std::int32_t locateTriangle(const NavMesh& mesh, const Vec2i64& p) {
		const NavGrid& g = mesh.grid;
		// Empty mesh or grid not built (no triangles): off-mesh.
		if (g.cols == 0 || g.rows == 0) {
			return -1;
		}
		// Reject p outside the mesh AABB immediately.
		if (p.x < g.minPt.x || p.y < g.minPt.y || p.x > g.maxPt.x || p.y > g.maxPt.y) {
			return -1;
		}
		// Map p to its grid cell (safe: p is within [minPt, maxPt]).
		const std::int32_t col = static_cast<std::int32_t>((p.x - g.minPt.x) / g.cellSize);
		const std::int32_t row = static_cast<std::int32_t>((p.y - g.minPt.y) / g.cellSize);
		// Clamp to valid cell range (can happen when p == maxPt exactly).
		const std::int32_t c	   = col < g.cols ? col : g.cols - 1;
		const std::int32_t r	   = row < g.rows ? row : g.rows - 1;
		const std::int32_t cellIdx = r * g.cols + c;
		const std::int32_t begin   = g.cellStart[static_cast<std::size_t>(cellIdx)];
		const std::int32_t end	   = g.cellStart[static_cast<std::size_t>(cellIdx) + 1];
		// A point on an edge or vertex is in several triangles: the one whose sorted
		// corners come first answers, so the answer is the geometry's, as
		// PatchableMesh::locate's is.
		std::int32_t		   best = -1;
		std::array<Vec2i64, 3> bestCorners;
		for (std::int32_t i = begin; i < end; ++i) {
			const std::int32_t ti = g.candidates[static_cast<std::size_t>(i)];
			const NavTriangle& t  = mesh.triangles[static_cast<std::size_t>(ti)];
			const Vec2i64&	   v0 = mesh.vertices[t.v[0]];
			const Vec2i64&	   v1 = mesh.vertices[t.v[1]];
			const Vec2i64&	   v2 = mesh.vertices[t.v[2]];
			const Orientation  o0 = orientation(v0, v1, p);
			const Orientation  o1 = orientation(v1, v2, p);
			const Orientation  o2 = orientation(v2, v0, p);
			if (o0 == Orientation::Clockwise || o1 == Orientation::Clockwise || o2 == Orientation::Clockwise) {
				continue;
			}
			if (o0 == Orientation::CounterClockwise && o1 == Orientation::CounterClockwise && o2 == Orientation::CounterClockwise) {
				return ti;
			}
			std::array<Vec2i64, 3> corners{v0, v1, v2};
			std::sort(corners.begin(), corners.end());
			if (best < 0 || corners < bestCorners) {
				best		= ti;
				bestCorners = corners;
			}
		}
		return best;
	}

	ReachFrom reachFrom(const NavView& meshIn, const Vec2i64& start, std::int64_t agentRadiusMm, BeliefFilter belief) {
		const NavView mesh = meshIn.resolved();
		assert(forestFor(mesh, belief) != nullptr && "reachFrom: the mesh has no reachability forests");
		ReachFrom from;
		if (forestFor(mesh, belief) == nullptr) {
			return from; // reaches() answers "maybe" without forests
		}
		const std::int32_t startTri = standOn(mesh, mesh.locate(start), start, belief);
		// A start on a face this belief cannot stand on reaches nothing, same as pathThrough's
		// endpoint gate (e.g. start inside a wall the agent knows blocks).
		if (startTri < 0 || !traversable(mesh, startTri, belief)) {
			return from;
		}
		from.startTri = startTri;
		from.root	  = reachRootInForest(*forestFor(mesh, belief), startTri, discDiameterMm(agentRadiusMm));
		return from;
	}

	bool reaches(const NavView& meshIn, const ReachFrom& from, const Vec2i64& goal, BeliefFilter belief) {
		const NavView mesh = meshIn.resolved();
		assert(forestFor(mesh, belief) != nullptr && "reaches: the mesh has no reachability forests");
		if (forestFor(mesh, belief) == nullptr) {
			return true; // nothing proves the goal unreachable
		}
		if (from.startTri < 0) {
			return false; // off-mesh or untraversable start
		}
		const std::int32_t goalTri = standOn(mesh, mesh.locate(goal), goal, belief);
		if (goalTri < 0 || !traversable(mesh, goalTri, belief)) {
			return false;
		}
		if (goalTri == from.startTri || !belief.prices.empty()) {
			return true; // same triangle, or faces crossed at a price that the forest walls off
		}
		// `false` = the forest proves unreachability (sound); otherwise "maybe".
		return inForestSubtree(*forestFor(mesh, belief), from.root, goalTri);
	}

	PathResult pathThrough(const NavView& meshIn, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief) {
		const NavView mesh = meshIn.resolved();
		PathResult	  result;
		assert(mesh.widthsCurrent() && "pathThrough: corridor widths lag behind a patch");
		if (!mesh.widthsCurrent()) {
			return result; // stale widths could route a disc through a gap it does not fit
		}
		for (std::size_t i = 0; i < belief.prices.size(); ++i) {
			assert(belief.prices[i].multiplier > 1.0 && std::isfinite(belief.prices[i].multiplier) && "pathThrough: a price is above 1");
			for (std::size_t j = 0; j < i; ++j) {
				assert(belief.prices[j].tag != belief.prices[i].tag && "pathThrough: a tag priced twice");
			}
		}

		const std::int32_t startTri	   = standOn(mesh, mesh.locate(start), start, belief);
		const std::int32_t locatedGoal = mesh.locate(goal);
		const std::int32_t goalTri	   = standOn(mesh, locatedGoal, goal, belief);
		if (startTri < 0 || goalTri < 0) {
			return result; // off-mesh
		}

		// Reject a query whose endpoints sit on an untraversable face for this belief
		// (e.g. start inside a wall the agent knows blocks): no path. Without this, A*
		// could exit a blocked start through a floor neighbor and "escape" a wall.
		if (!traversable(mesh, startTri, belief) || !traversable(mesh, goalTri, belief)) {
			return result;
		}

		if (startTri == goalTri) {
			const double m			  = stepMultiplier(mesh, startTri, belief);
			const double length		  = distanceD(toD(start), toD(goal));
			result.reachable		  = true;
			result.points			  = {start, goal};
			result.cost				  = length * m;
			result.crossesPricedFaces = m > 1.0 && length > 0.0;
			return result;
		}

		// Disc diameter the width gates compare against. Computed once; both the
		// reachability short-circuit and the A* width filter use this exact value, so
		// they never disagree on the threshold.
		const std::int64_t diameterMm = discDiameterMm(agentRadiusMm);

		// Width-aware reachability short-circuit (P3.3): reject a provably-unreachable
		// goal before paying for the search. Sound: a `true` from the forest is "maybe"
		// and falls through to the A* (which decides for real); a reject is a proof no
		// path admits this disc.
		// Faces crossed at a price are walls to the forests, so they prove nothing then.
		const bool priced = !belief.prices.empty();
		if (!priced && reachabilityRejects(mesh, startTri, goalTri, diameterMm, belief)) {
			return result; // empty PathResult: definitely unreachable
		}

		// --- Triangle A* over the dual graph, width-filtered -------------------
		// Search state is (triangle, entry-edge): the squeeze the agent must clear to
		// LEAVE a triangle depends on which edge it ENTERED by (Demyen edge-pair width
		// keyed by the apex shared by the entry and exit edges), so the same triangle is
		// distinct per entry edge. entry == 3 is the start triangle (no entry edge, no
		// squeeze). At most 4 states per triangle.
		//
		// A label is one corridor reaching a state, carrying the corridor's funnel (see
		// Funnel), scored by the funnel's length on to the goal: what the corridor's taut
		// path would be if the rest were open ground, and never more than it. A corridor's
		// score depends on where its walls and corners are, not on how its open ground is
		// cut into triangles, so seams and free diagonals do not steer the search. A state
		// keeps up to kLabelsPerState labels (see there). A label popped in the goal
		// triangle is pulled exactly and pushed again at that length (or cost); the first
		// to pop at its exact length is the corridor the search returns.
		const int kStartEntry = 3;
		auto	  stateId	  = [](std::int32_t tri, int entry) { return tri * 4 + entry; };

		// A state keeps corridors that cross priced faces apart from those that do not, so
		// a cheap-looking way through never crowds out a free way round.
		SearchScratch& scratch = searchScratch(static_cast<std::size_t>(mesh.triangleSlots()) * 8);
		CornerMemo& corners = scratch.corners.begin(mesh);
		// A squeeze past detail-class faces (a gate the full width fails and the structural
		// width passes) costs the dearest detail price, when the query prices every detail
		// tag; with any left unpriced, detail is hard and such a gap stays shut.
		double squeezePrice = 0.0;
		// The least price a query sets: a priced run costs at least that over the disc's
		// diameter, whichever tags it holds (see walkCorridor), which the search's bound
		// counts as soon as a run starts.
		double leastPrice = 0.0;
		for (const TagPrice& p : belief.prices) {
			leastPrice = leastPrice > 0.0 ? std::min(leastPrice, p.multiplier) : p.multiplier;
		}
		const double runFloor = leastPrice > 0.0 ? (leastPrice - 1.0) * static_cast<double>(diameterMm) : 0.0;
		if (priced && mesh.detailTags != nullptr) {
			for (const std::int64_t tag : *mesh.detailTags) {
				const auto it = std::find_if(belief.prices.begin(), belief.prices.end(), [&](const TagPrice& p) { return p.tag == tag; });
				if (it == belief.prices.end()) {
					squeezePrice = 0.0;
					break;
				}
				squeezePrice = std::max(squeezePrice, it->multiplier);
			}
		}

		const Vec2d	 goalD	= toD(goal);
		const double radius = static_cast<double>(agentRadiusMm);

		auto scoreFrom = [&](const Funnel& f, const Portal& portal) { return walkVia(f, portal, goalD); };

		// Open-set node: score, then a key mixed from geometry (the triangle's corners,
		// the entry edge's ends, the apex), so equal scores resolve the same way whatever
		// slots the triangles sit in. std::priority_queue is a max-heap, hence the
		// reversed comparisons.
		struct Node {
			double		  f;
			double		  near; // distance on from the entry portal's middle: nearer the goal first
			std::uint64_t tie;
			std::int32_t  label;
		};
		struct NodeWorse {
			bool operator()(const Node& a, const Node& b) const {
				if (a.f != b.f) {
					return a.f > b.f;
				}
				if (a.near != b.near) {
					return a.near > b.near;
				}
				return a.tie > b.tie;
			}
		};
		std::priority_queue<Node, std::vector<Node>, NodeWorse> open;
		auto tieOf = [&](const NavTriangle& t, const Vec2i64& entry, const Vec2d& apex) {
			const Vec2i64 corners = cornerSum(mesh, t);
			std::uint64_t key	  = mixKey(static_cast<std::uint64_t>(corners.x), static_cast<std::uint64_t>(corners.y));
			key					  = mixKey(mixKey(key, static_cast<std::uint64_t>(entry.x)), static_cast<std::uint64_t>(entry.y));
			return mixKey(mixKey(key, std::bit_cast<std::uint64_t>(apex.x)), std::bit_cast<std::uint64_t>(apex.y));
		};

		const std::int32_t startState = stateId(startTri, kStartEntry);
		const Vec2d		   startD	  = toD(start);
		{
			Label first;
			first.funnel = funnelAt(startD, 0);
			first.entry	 = {startD, startD, 0};
			first.best	 = scoreFrom(first.funnel, first.entry);
			first.state	 = startState;
			first.priced = stepMultiplier(mesh, startTri, belief) > 1.0;
			scratch.labels.push_back(first);
			StateLabels& held = scratch.held(startState * 2 + (first.priced ? 1 : 0));
			held.ids[0]		  = 0;
			held.count		  = 1;
			open.push({first.best, distanceD(startD, goalD), tieOf(mesh.triangle(startTri), start + start, startD), 0});
		}
		result.peakOpenSet = static_cast<std::int64_t>(open.size());
		std::vector<Portal>& replay = scratch.replay;

		// Pull one more portal into a label's funnel. When the path bends at a corner from
		// an earlier portal, the funnel restarts there with this portal alone: looser than
		// the string-pull's, which would narrow it by every portal since, so its scores stay
		// lower bounds; a goal label is pulled exactly before it is accepted.
		auto advance = [&](Funnel& funnel, const Portal& step) {
			if (narrow(funnel, step)) {
				return;
			}
			replay.assign(1, step);
			pull(funnel, replay, nullptr);
		};

		// The string-pull of label li's corridor, every portal from the start: its points
		// into `points`, and its cost, which is its length unless the corridor crosses a
		// face walked at a price. Then each piece inside a priced triangle is weighted by
		// its multiplier, but a run of priced triangles costs at least its least multiplier
		// over the disc's diameter: a disc entering priced ground overlaps it that far
		// however little of the centreline is inside (a corner shaved through the face's
		// own triangle has none). A squeeze dearer than the triangle it leaves charges its
		// surplus over at most the diameter of the piece, the stretch where the disc overlaps
		// what narrows the gap (a large triangle's piece can run far from it); a squeeze
		// beside the priced run it crosses is that run's (see the width gate).
		bool   crossesPriced = false;
		double cost			 = 0.0;
		double runSurplus	 = 0.0;
		double runLeast		 = 0.0; // the least multiplier of the priced run the walk is in, 0 outside one
		auto   closeRun		 = [&]() {
			  if (runLeast > 0.0) {
				  const double surplus = std::max(runSurplus, (runLeast - 1.0) * static_cast<double>(diameterMm));
				  cost += surplus;
				  crossesPriced = crossesPriced || surplus > 0.0;
			  }
			  runSurplus = 0.0;
			  runLeast	 = 0.0;
		};
		auto addPiece = [&](std::int32_t t, double squeeze, double piece) {
			const double m = stepMultiplier(mesh, t, belief);
			cost += piece;
			if (m > 1.0) {
				runSurplus += (m - 1.0) * piece;
				runLeast = runLeast > 0.0 ? std::min(runLeast, m) : m;
			} else {
				closeRun();
			}
			if (squeeze > m) {
				cost += (squeeze - m) * std::min(piece, static_cast<double>(diameterMm));
				crossesPriced = crossesPriced || diameterMm > 0;
			}
		};
		auto walkCorridor = [&](std::int32_t li, std::vector<Vec2i64>& points) {
			std::vector<std::int32_t>& tris	   = scratch.corridor;
			std::vector<double>&	   squeeze = scratch.squeeze; // squeeze[k]: the price of leaving tris[k]
			replay.clear();
			tris.clear();
			squeeze.clear();
			double leaving = 1.0;
			for (std::int32_t at = li; at >= 0; at = scratch.labels[static_cast<std::size_t>(at)].cameFrom) {
				tris.push_back(scratch.labels[static_cast<std::size_t>(at)].state / 4);
				squeeze.push_back(leaving);
				leaving = scratch.labels[static_cast<std::size_t>(at)].squeeze;
				if (at > 0) {
					replay.push_back(scratch.labels[static_cast<std::size_t>(at)].entry);
				}
			}
			std::reverse(replay.begin(), replay.end());
			std::reverse(tris.begin(), tris.end());
			std::reverse(squeeze.begin(), squeeze.end());
			replay.push_back({goalD, goalD, -1});
			points.assign(1, start);
			Funnel taut = funnelAt(startD, -1);
			pull(taut, replay, &points);
			appendTaut(points, goal);
			crossesPriced = false;
			if (!scratch.labels[static_cast<std::size_t>(li)].priced) {
				return lengthTo(taut, goalD);
			}
			// Walk the path through the corridor: triangle k ends where the path crosses
			// to the far side of the edge it shares with triangle k + 1.
			cost			  = 0.0;
			runSurplus		  = 0.0;
			runLeast		  = 0.0;
			double		piece = 0.0;
			std::size_t s	  = 0;
			Vec2d		at	  = toD(points[0]);
			for (std::size_t k = 0; k + 1 < tris.size(); ++k) {
				const NavTriangle& tri = mesh.triangle(tris[k]);
				const int		   e   = sharedEdgeIndex(tri, tris[k + 1]);
				const Vec2d		   a   = toD(mesh.vertex(tri.v[e]));
				const Vec2d		   b   = toD(mesh.vertex(tri.v[(e + 1) % 3]));
				const double	   len = std::max(distanceD(a, b), 1e-9);
				auto			   side = [&](const Vec2d& p) { return area2(a, b, p) / len; }; // mm, positive inside tri
				while (s + 1 < points.size()) {
					const Vec2d	 q	= toD(points[s + 1]);
					const double sq = side(q);
					if (sq > 0.5) {
						piece += distanceD(at, q);
						at = q;
						++s;
						continue;
					}
					const double sa	  = side(at);
					const double t	  = sa > sq ? std::clamp(sa / (sa - sq), 0.0, 1.0) : 0.0;
					const Vec2d	 cross = at + (q - at) * t;
					piece += distanceD(at, cross);
					at = cross;
					break;
				}
				addPiece(tris[k], squeeze[k], piece);
				piece = 0.0;
			}
			while (s + 1 < points.size()) {
				const Vec2d q = toD(points[s + 1]);
				piece += distanceD(at, q);
				at = q;
				++s;
			}
			addPiece(tris.back(), squeeze.back(), piece);
			closeRun();
			return cost;
		};

		std::int32_t goalLabel = -1;
		while (!open.empty()) {
			const Node cur = open.top();
			open.pop();
			const std::int32_t li = cur.label;
			if (scratch.labels[static_cast<std::size_t>(li)].closed || cur.f != scratch.labels[static_cast<std::size_t>(li)].best) {
				continue; // stale entry (we push duplicates instead of decrease-key)
			}
			scratch.labels[static_cast<std::size_t>(li)].closed = true;
			++result.nodesExpanded; // a label closed exactly once: one real expansion
			const std::int32_t s	 = scratch.labels[static_cast<std::size_t>(li)].state;
			const std::int32_t ti	 = s / 4;
			const int		   entry = s % 4;
			if (ti == goalTri) {
				// The score is a bound; the corridor's taut length to the goal is exact. Only
				// a label whose exact length is still the least in the open set is accepted.
				Label& at = scratch.labels[static_cast<std::size_t>(li)];
				if (!at.exact) {
					at.exact			= true;
					at.closed			= false;
					const double length = walkCorridor(li, scratch.points);
					if (length > at.best) {
						at.best = length;
						open.push({length, cur.near, cur.tie, li});
						--result.nodesExpanded;
						continue;
					}
				}
				goalLabel = li;
				break;
			}

			const NavTriangle& tri	  = mesh.triangle(ti);
			const Funnel	   funnel = scratch.labels[static_cast<std::size_t>(li)].funnel;
			for (int e = 0; e < 3; ++e) {
				if (e == entry) {
					continue; // U-turn back through the entry edge: never on a taut path
				}
				const std::int32_t nb = tri.neighbor[e];
				if (nb < 0) {
					continue;
				}
				// Belief gate: skip a neighbor this agent may not cross. The only place
				// truth/belief changes routing; the funnel below is untouched.
				const double nbMultiplier = stepMultiplier(mesh, nb, belief);
				if (nbMultiplier <= 0.0) {
					continue;
				}
				// Width gate (intra-triangle squeeze): to leave `ti` via edge e having
				// entered by `entry`, the disc must clear the Demyen width of the edge
				// pair meeting at their shared apex. The start state has no entry edge,
				// so nothing to clear inside the start triangle.
				// A gap too narrow for the disc that only detail narrows (its structural width
				// admits the disc) is squeezed through at squeezePrice; anything else stays shut.
				double squeeze = 1.0;
				if (entry != kStartEntry && !widthAdmits(tri, apexVertexOfEdgePair(entry, e), diameterMm)) {
					if (squeezePrice <= 0.0 || tri.structuralWidthMm[apexVertexOfEdgePair(entry, e)] < diameterMm) {
						continue;
					}
					squeeze = squeezePrice;
					// Beside a priced face the corridor walks into, came out of, or is in, the
					// disc's overlap with it is that crossing, already paid: only a dearer
					// narrowing is charged.
					const std::int32_t from	  = scratch.labels[static_cast<std::size_t>(li)].cameFrom;
					const double	   before = from >= 0 ? stepMultiplier(mesh, scratch.labels[static_cast<std::size_t>(from)].state / 4, belief) : 1.0;
					if (squeeze <= std::max({nbMultiplier, before, stepMultiplier(mesh, ti, belief)})) {
						squeeze = 1.0;
					}
				}
				// Width gate (door portal): a door edge is gated by its clear width.
				if (tri.edgeOpening[e] != kNoOpening && tri.edgeClearWidthMm[e] < diameterMm) {
					continue;
				}

				// The edge of `nb` that faces `ti` is nb's entry edge for this step.
				const int back = sharedEdgeIndex(mesh.triangle(nb), ti);
				if (back < 0) {
					continue; // adjacency inconsistency; skip safely
				}
				const std::int32_t ns	 = stateId(nb, back);
				const bool	 freshPriced = scratch.labels[static_cast<std::size_t>(li)].priced || nbMultiplier > 1.0 || squeeze > 1.0;
				StateLabels& held		 = scratch.held(ns * 2 + (freshPriced ? 1 : 0));
				if (held.count == kLabelsPerState) {
					bool anyOpen = false;
					for (int k = 0; k < held.count; ++k) {
						anyOpen = anyOpen || !scratch.labels[static_cast<std::size_t>(held.ids[static_cast<std::size_t>(k)])].closed;
					}
					if (!anyOpen) {
						continue; // full of expanded corridors: nothing this one could take over
					}
				}
				const std::int32_t label = static_cast<std::int32_t>(scratch.labels.size()); // the label a new one would take
				const PortalSpan   span	 = portalSpan(mesh, ti, e, radius, belief, corners);
				const Portal	   step{span.left, span.right, label};
				Funnel			   next = funnel;
				advance(next, step);
				// What priced faces add, as a bound: a run of them is entered, carried on, or
				// left (charged then for the distance across it, or its least price over the
				// disc's diameter, as walkCorridor does) by this step.
				const Label& parent	  = scratch.labels[static_cast<std::size_t>(li)];
				double		 extra	  = parent.extra;
				double		 runPrice = 0.0;
				Portal		 runEntry = step;
				if (parent.runPrice > 0.0) {
					const double across = (parent.runPrice - 1.0) * segmentToSegment(parent.runEntry.left, parent.runEntry.right, step.left, step.right);
					if (nbMultiplier > 1.0) {
						runPrice = std::min(parent.runPrice, nbMultiplier);
						runEntry = parent.runEntry;
					} else {
						extra += std::max(across, (parent.runPrice - 1.0) * static_cast<double>(diameterMm));
					}
				} else if (nbMultiplier > 1.0) {
					runPrice = nbMultiplier;
				}
				const double inRun = runPrice > 0.0 ? std::max((runPrice - 1.0) * segmentToSegment(runEntry.left, runEntry.right, step.left, step.right), runFloor) : 0.0;
				const double score = scoreFrom(next, step) + extra + inRun;
				std::int32_t slot = -1; // the label this corridor takes over, if any
				bool		 beaten = false;
				std::array<double, 3> reach = held.count > 0 ? reachOf(next, step) : std::array<double, 3>{};
				for (double& r : reach) {
					r += extra + inRun;
				}
				for (int k = 0; k < held.count && !beaten; ++k) {
					Label&						other	   = scratch.labels[static_cast<std::size_t>(held.ids[static_cast<std::size_t>(k)])];
					std::array<double, 3>		otherReach = reachOf(other.funnel, step);
					const double				otherExtra = other.extra + (other.runPrice > 0.0 ? std::max((other.runPrice - 1.0) * segmentToSegment(other.runEntry.left, other.runEntry.right, step.left, step.right), runFloor) : 0.0);
					for (double& r : otherReach) {
						r += otherExtra;
					}
					// A goal label pulled exactly is known by its walk, not its funnel (whose
					// reach is a bound from a restarted cone): it beats only a corridor whose own
					// bound is no better.
					if (other.exact ? score >= other.best : noLonger(otherReach, reach)) {
						beaten = true;
					} else if (!other.closed && !other.exact && noLonger(reach, otherReach)) {
						if (slot < 0) {
							slot = held.ids[static_cast<std::size_t>(k)];
						} else {
							other.closed = true; // dominated twice over: retire it
						}
					}
				}
				if (beaten) {
					continue;
				}
				if (slot < 0 && held.count == kLabelsPerState) {
					// Full: take over the worst-scoring open corridor if this one scores better.
					// A goal label already pulled exactly is never taken over: its score is its
					// walk, and a newcomer's is only a bound that can come out longer.
					for (int k = 0; k < held.count; ++k) {
						const std::int32_t id = held.ids[static_cast<std::size_t>(k)];
						const Label&	   other = scratch.labels[static_cast<std::size_t>(id)];
						if (!other.closed && !other.exact && other.best > score && (slot < 0 || other.best > scratch.labels[static_cast<std::size_t>(slot)].best)) {
							slot = id;
						}
					}
					if (slot < 0) {
						continue;
					}
				}
				Label fresh;
				fresh.funnel   = next;
				fresh.entry	   = step;
				fresh.best	   = score;
				fresh.state	   = ns;
				fresh.cameFrom = li;
				fresh.priced   = freshPriced;
				fresh.squeeze  = squeeze;
				fresh.extra	   = extra;
				fresh.runEntry = runEntry;
				fresh.runPrice = runPrice;
				if (slot < 0) {
					slot = label;
					held.ids[static_cast<std::size_t>(held.count++)] = label;
					scratch.labels.push_back(fresh);
				} else {
					// The new corridor takes over an open label; the portal it records as
					// its own must name the label it now is.
					fresh.entry.label = slot;
					if (fresh.funnel.leftFrom == label) {
						fresh.funnel.leftFrom = slot;
					}
					if (fresh.funnel.rightFrom == label) {
						fresh.funnel.rightFrom = slot;
					}
					if (fresh.funnel.apexFrom == label) {
						fresh.funnel.apexFrom = slot;
					}
					scratch.labels[static_cast<std::size_t>(slot)] = fresh;
				}
				open.push({score, distanceD((step.left + step.right) * 0.5, goalD), tieOf(mesh.triangle(nb), edgeSum(mesh, tri, e), next.apex), slot});
				const std::int64_t sz = static_cast<std::int64_t>(open.size());
				if (sz > result.peakOpenSet) {
					result.peakOpenSet = sz;
				}
			}
		}

		if (goalLabel < 0) {
			return result; // disconnected (or every corridor too narrow for the agent)
		}

		// --- The winning corridor's string-pull ----------------------------------
		std::vector<Vec2i64> pts;
		result.cost				  = walkCorridor(goalLabel, pts);
		result.crossesPricedFaces = crossesPriced;

		result.reachable = true;
		result.points	 = std::move(pts);
		return result;
	}

	ReachFrom reachFrom(const NavMesh& mesh, const Vec2i64& start, std::int64_t agentRadiusMm, BeliefFilter belief) {
		return reachFrom(navView(mesh), start, agentRadiusMm, belief);
	}

	bool reaches(const NavMesh& mesh, const ReachFrom& from, const Vec2i64& goal, BeliefFilter belief) {
		return reaches(navView(mesh), from, goal, belief);
	}

	PathResult pathThrough(const NavMesh& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief) {
		return pathThrough(navView(mesh), start, goal, agentRadiusMm, belief);
	}

} // namespace geometry::nav
