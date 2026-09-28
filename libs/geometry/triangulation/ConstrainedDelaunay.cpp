#include "ConstrainedDelaunay.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>

namespace geometry {

	namespace {

		constexpr std::int32_t kNone = -1;

		int next3(int i) {
			return i == 2 ? 0 : i + 1;
		}

		int prev3(int i) {
			return i == 0 ? 2 : i - 1;
		}

		class CdtBuilder {
		  public:
			explicit CdtBuilder(const std::vector<Vec2i64>& input) : vertices(input) {
				frameStart		   = static_cast<std::uint32_t>(input.size());
				Vec2i64 lo		   = input.front();
				Vec2i64 hi		   = input.front();
				for (const Vec2i64& p : input) {
					lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
					hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
				}
				const std::int64_t margin = std::max({hi.x - lo.x, hi.y - lo.y, std::int64_t{1}});
				vertices.push_back({lo.x - margin, lo.y - margin});
				vertices.push_back({hi.x + margin, lo.y - margin});
				vertices.push_back({hi.x + margin, hi.y + margin});
				vertices.push_back({lo.x - margin, hi.y + margin});
				const std::uint32_t c = frameStart;
				tris.push_back({{c, c + 1, c + 2}, {kNone, kNone, 1}, {kNone, kNone, kNone}});
				tris.push_back({{c, c + 2, c + 3}, {0, kNone, kNone}, {kNone, kNone, kNone}});
			}

			void insertVertices() {
				std::int32_t hint = 0;
				for (std::uint32_t p = 0; p < frameStart; ++p) {
					hint = insertVertex(p, hint);
				}
				vertexTri.assign(vertices.size(), kNone);
				for (std::int32_t t = 0; t < static_cast<std::int32_t>(tris.size()); ++t) {
					for (std::uint32_t v : tris[static_cast<std::size_t>(t)].v) {
						vertexTri[v] = t;
					}
				}
				mark.assign(tris.size(), 0);
			}

			void insertConstraint(std::int32_t id, std::uint32_t a, std::uint32_t b) {
				while (a != b) {
					const std::uint32_t reached = insertConstraintPiece(id, a, b);
					if (reached == a) {
						return; // blocked by a crossing constraint (precondition violated)
					}
					a = reached;
				}
			}

			// Lawson flips over every unconstrained edge until each is locally Delaunay.
			// Constraint recovery already yields the constrained Delaunay triangulation, so
			// this normally flips nothing; it is what guarantees the property regardless.
			void restoreDelaunay() {
				stack.clear();
				for (std::int32_t t = 0; t < static_cast<std::int32_t>(tris.size()); ++t) {
					for (int i = 0; i < 3; ++i) {
						if (tri(t).neighbor[i] > t) {
							stack.push_back({t, i});
						}
					}
				}
				while (!stack.empty()) {
					const auto [t, i] = stack.back();
					stack.pop_back();
					if (!isIllegal(t, i)) {
						continue;
					}
					const std::int32_t u = flip(t, i);
					stack.push_back({t, 0});
					stack.push_back({t, 1});
					stack.push_back({u, 1});
					stack.push_back({u, 2});
				}
			}

			ConstrainedDelaunay finish() {
				ConstrainedDelaunay out;
				out.vertices   = std::move(vertices);
				out.triangles  = std::move(tris);
				out.frameStart = frameStart;
				return out;
			}

		  private:
			// A cavity edge that stays: its outside neighbor and that neighbor's edge
			// back into the cavity, so the new triangle can take its place.
			struct BoundaryEdge {
				std::uint32_t from		 = 0;
				std::uint32_t to		 = 0;
				std::int32_t  outside	 = kNone;
				int			  outsideEdge = 0;
				std::int32_t  constraint = kNone;
			};

			// A pseudo-polygon still to triangulate: base (a, b) and chain[lo, hi), the
			// chain running from a's end to b's end, strictly left of a->b.
			struct PseudoPolygon {
				std::uint32_t a;
				std::uint32_t b;
				std::size_t	  lo;
				std::size_t	  hi;
			};

			CdtTriangle& tri(std::int32_t t) {
				return tris[static_cast<std::size_t>(t)];
			}

			const Vec2i64& at(std::uint32_t v) const {
				return vertices[v];
			}

			static int indexOf(const CdtTriangle& t, std::uint32_t v) {
				return t.v[0] == v ? 0 : (t.v[1] == v ? 1 : 2);
			}

			static int edgeTo(const CdtTriangle& t, std::int32_t nb) {
				return t.neighbor[0] == nb ? 0 : (t.neighbor[1] == nb ? 1 : 2);
			}

			// Index of t's edge running from -> to, or -1.
			static int directedEdge(const CdtTriangle& t, std::uint32_t from, std::uint32_t to) {
				for (int i = 0; i < 3; ++i) {
					if (t.v[i] == from && t.v[next3(i)] == to) {
						return i;
					}
				}
				return -1;
			}

			void replaceNeighbor(std::int32_t t, std::int32_t from, std::int32_t to) {
				if (t != kNone) {
					CdtTriangle& x				  = tri(t);
					x.neighbor[edgeTo(x, from)] = to;
				}
			}

			std::int32_t addTriangle() {
				tris.push_back({});
				return static_cast<std::int32_t>(tris.size()) - 1;
			}

			// Visibility walk. The triangulation is Delaunay whenever this runs (every
			// vertex goes in before any constraint), and a walk in a Delaunay
			// triangulation never cycles (Edelsbrunner).
			std::int32_t locate(const Vec2i64& p, std::int32_t t) const {
				std::int32_t cameFrom = kNone;
				for (;;) {
					const CdtTriangle& x	= tris[static_cast<std::size_t>(t)];
					std::int32_t	   step = kNone;
					for (int i = 0; i < 3; ++i) {
						if (x.neighbor[i] != cameFrom && orientation(at(x.v[i]), at(x.v[next3(i)]), p) == Orientation::Clockwise) {
							step = x.neighbor[i];
							break;
						}
					}
					if (step == kNone) {
						return t;
					}
					cameFrom = t;
					t		 = step;
				}
			}

			std::int32_t insertVertex(std::uint32_t p, std::int32_t hint) {
				const std::int32_t t	  = locate(at(p), hint);
				const CdtTriangle& x	  = tri(t);
				int				   onEdge = -1;
				int				   onLines = 0;
				for (int i = 0; i < 3; ++i) {
					if (orientation(at(x.v[i]), at(x.v[next3(i)]), at(p)) == Orientation::Collinear) {
						onEdge = i;
						++onLines;
					}
				}
				assert(onLines < 2 && "buildConstrainedDelaunay: duplicate vertex");
				if (onLines >= 2) {
					return t;
				}
				stack.clear();
				if (onLines == 1) {
					splitEdge(t, onEdge, p);
				} else {
					splitTriangle(t, p);
				}
				legalizeStack();
				return t;
			}

			// p strictly inside t = (a, b, c): three triangles fanned around p, each with p
			// at index 0 so the edge opposite p is edge 1.
			void splitTriangle(std::int32_t t, std::uint32_t p) {
				const CdtTriangle	old = tri(t);
				const std::uint32_t a	= old.v[0];
				const std::uint32_t b	= old.v[1];
				const std::uint32_t c	= old.v[2];
				const std::int32_t	t1	= addTriangle();
				const std::int32_t	t2	= addTriangle();
				tri(t)					= {{p, a, b}, {t2, old.neighbor[0], t1}, {kNone, old.constraint[0], kNone}};
				tri(t1)					= {{p, b, c}, {t, old.neighbor[1], t2}, {kNone, old.constraint[1], kNone}};
				tri(t2)					= {{p, c, a}, {t1, old.neighbor[2], t}, {kNone, old.constraint[2], kNone}};
				replaceNeighbor(old.neighbor[1], t, t1);
				replaceNeighbor(old.neighbor[2], t, t2);
				stack.push_back({t, 1});
				stack.push_back({t1, 1});
				stack.push_back({t2, 1});
			}

			// p in the open interior of edge i of t, shared with u: t = (x, y, z) and
			// u = (y, x, w) become four triangles around p. The halves of a split
			// constraint keep its index.
			void splitEdge(std::int32_t t, int i, std::uint32_t p) {
				const CdtTriangle	told = tri(t);
				const std::int32_t	u	 = told.neighbor[i];
				const CdtTriangle	uold = tri(u);
				const int			j	 = edgeTo(uold, t);
				const std::uint32_t x	 = told.v[i];
				const std::uint32_t y	 = told.v[next3(i)];
				const std::uint32_t z	 = told.v[prev3(i)];
				const std::uint32_t w	 = uold.v[prev3(j)];
				const std::int32_t	cxy	 = told.constraint[i];
				const std::int32_t	nyz	 = told.neighbor[next3(i)];
				const std::int32_t	nzx	 = told.neighbor[prev3(i)];
				const std::int32_t	nxw	 = uold.neighbor[next3(j)];
				const std::int32_t	nwy	 = uold.neighbor[prev3(j)];
				const std::int32_t	ty	 = addTriangle();
				const std::int32_t	tx	 = addTriangle();
				tri(t)	= {{p, z, x}, {ty, nzx, tx}, {kNone, told.constraint[prev3(i)], cxy}};
				tri(ty) = {{p, y, z}, {u, nyz, t}, {cxy, told.constraint[next3(i)], kNone}};
				tri(u)	= {{p, w, y}, {tx, nwy, ty}, {kNone, uold.constraint[prev3(j)], cxy}};
				tri(tx) = {{p, x, w}, {t, nxw, u}, {cxy, uold.constraint[next3(j)], kNone}};
				replaceNeighbor(nyz, t, ty);
				replaceNeighbor(nxw, u, tx);
				stack.push_back({t, 1});
				stack.push_back({ty, 1});
				stack.push_back({u, 1});
				stack.push_back({tx, 1});
			}

			// Edge i of t is illegal when it is unconstrained and the far apex lies
			// strictly inside t's circumcircle. Such a quad is always strictly convex, so
			// the flip is valid.
			bool isIllegal(std::int32_t t, int i) {
				const CdtTriangle& x = tri(t);
				const std::int32_t u = x.neighbor[i];
				if (u == kNone || x.constraint[i] != kNone) {
					return false;
				}
				const CdtTriangle&	y = tri(u);
				const std::uint32_t d = y.v[prev3(edgeTo(y, t))];
				return inCircle(at(x.v[0]), at(x.v[1]), at(x.v[2]), at(d)) == InCircle::Inside;
			}

			// Flip edge i of t = (x, y, p) against u = (y, x, d): t becomes (p, x, d) and u
			// becomes (p, d, y), so p stays at index 0 of both. Returns u.
			std::int32_t flip(std::int32_t t, int i) {
				const CdtTriangle	told = tri(t);
				const std::int32_t	u	 = told.neighbor[i];
				const CdtTriangle	uold = tri(u);
				const int			j	 = edgeTo(uold, t);
				const std::uint32_t x	 = told.v[i];
				const std::uint32_t y	 = told.v[next3(i)];
				const std::uint32_t p	 = told.v[prev3(i)];
				const std::uint32_t d	 = uold.v[prev3(j)];
				const std::int32_t	nxd	 = uold.neighbor[next3(j)];
				const std::int32_t	nyp	 = told.neighbor[next3(i)];
				tri(t) = {{p, x, d}, {told.neighbor[prev3(i)], nxd, u}, {told.constraint[prev3(i)], uold.constraint[next3(j)], kNone}};
				tri(u) = {{p, d, y}, {t, uold.neighbor[prev3(j)], nyp}, {kNone, uold.constraint[prev3(j)], told.constraint[next3(i)]}};
				replaceNeighbor(nxd, u, t);
				replaceNeighbor(nyp, t, u);
				return u;
			}

			// After a vertex insertion only the edges opposite the new vertex (edge 1,
			// with the vertex at index 0) can turn illegal.
			void legalizeStack() {
				while (!stack.empty()) {
					const auto [t, i] = stack.back();
					stack.pop_back();
					if (!isIllegal(t, i)) {
						continue;
					}
					const std::int32_t u = flip(t, i);
					stack.push_back({t, 1});
					stack.push_back({u, 1});
				}
			}

			void markConstraint(std::int32_t t, int i, std::int32_t id) {
				CdtTriangle& x	   = tri(t);
				x.constraint[i]	   = id;
				CdtTriangle& y	   = tri(x.neighbor[i]);
				y.constraint[edgeTo(y, t)] = id;
			}

			// Recover the constraint from a toward b as far as the first vertex on the
			// segment (b itself, or a vertex lying on it). Returns that vertex, or a when a
			// crossing constraint blocks the way.
			std::uint32_t insertConstraintPiece(std::int32_t id, std::uint32_t a, std::uint32_t b) {
				const Vec2i64& pa = at(a);
				const Vec2i64& pb = at(b);

				// Turn counterclockwise around a until the segment either runs along an
				// edge or enters a triangle's interior between its other two vertices.
				std::int32_t t = vertexTri[a];
				if (t == kNone || vertexTri[b] == kNone) {
					return a; // an endpoint never made it in (a duplicate vertex)
				}
				int k = indexOf(tri(t), a);
				for (;;) {
					const CdtTriangle&	x = tri(t);
					const std::uint32_t u = x.v[next3(k)];
					const std::uint32_t w = x.v[prev3(k)];
					if (orientation(pa, pb, at(u)) == Orientation::Collinear && dot(at(u) - pa, pb - pa).sign() > 0) {
						markConstraint(t, k, id);
						return u;
					}
					if (orientation(pa, pb, at(u)) == Orientation::Clockwise &&
						orientation(pa, pb, at(w)) == Orientation::CounterClockwise) {
						break;
					}
					t = x.neighbor[prev3(k)];
					k = indexOf(tri(t), a);
				}

				// Walk the triangles the open segment crosses. The crossed edge always runs
				// from the right chain to the left chain.
				cavity.clear();
				leftChain.clear();
				rightChain.clear();
				cavity.push_back(t);
				rightChain.push_back(tri(t).v[next3(k)]);
				leftChain.push_back(tri(t).v[prev3(k)]);
				std::int32_t  cur = t;
				int			  e	  = next3(k);
				std::uint32_t end = b;
				for (;;) {
					if (tri(cur).constraint[e] != kNone) {
						assert(false && "buildConstrainedDelaunay: constraints cross");
						return a;
					}
					const std::int32_t	nb	 = tri(cur).neighbor[e];
					const int			back = edgeTo(tri(nb), cur);
					const std::uint32_t apex = tri(nb).v[prev3(back)];
					cavity.push_back(nb);
					if (apex == b) {
						break;
					}
					const Orientation side = orientation(pa, pb, at(apex));
					if (side == Orientation::Collinear) {
						end = apex; // a vertex on the segment ends this piece
						break;
					}
					cur = nb;
					if (side == Orientation::CounterClockwise) {
						leftChain.push_back(apex);
						e = next3(back);
					} else {
						rightChain.push_back(apex);
						e = prev3(back);
					}
				}

				retriangulateCavity(a, end, id);
				return end;
			}

			void retriangulateCavity(std::uint32_t a, std::uint32_t end, std::int32_t id) {
				++stamp;
				if (mark.size() < tris.size()) {
					mark.resize(tris.size(), 0);
				}
				for (std::int32_t t : cavity) {
					mark[static_cast<std::size_t>(t)] = stamp;
				}
				// An edge between two cavity triangles is usually a crossed edge, which goes.
				// The exception is a hanging edge: where the segment leaves a vertex's star and
				// re-enters it, the cavity wraps around an edge the segment never crosses, and
				// the chain visits its far end as a spike. The retriangulation rebuilds that
				// edge from both sides, so a constraint on it must carry over.
				boundary.clear();
				hanging.clear();
				for (std::int32_t t : cavity) {
					const CdtTriangle& x = tri(t);
					for (int i = 0; i < 3; ++i) {
						const std::int32_t nb = x.neighbor[i];
						if (nb != kNone && mark[static_cast<std::size_t>(nb)] == stamp) {
							if (x.constraint[i] != kNone) {
								hanging.push_back({x.v[i], x.v[next3(i)], kNone, 0, x.constraint[i]});
							}
							continue;
						}
						const int back = nb == kNone ? 0 : edgeTo(tri(nb), t);
						boundary.push_back({x.v[i], x.v[next3(i)], nb, back, x.constraint[i]});
					}
				}

				// Left pseudo-polygon on base a->end; the right one on end->a with its chain
				// reversed so it too runs from its base's start to its end.
				fresh.clear();
				triangulatePseudoPolygon(a, end, leftChain);
				std::reverse(rightChain.begin(), rightChain.end());
				triangulatePseudoPolygon(end, a, rightChain);
				assert(fresh.size() == cavity.size());

				for (std::size_t s = 0; s < cavity.size(); ++s) {
					tri(cavity[s]) = {fresh[s], {kNone, kNone, kNone}, {kNone, kNone, kNone}};
				}
				for (std::size_t s = 0; s < cavity.size(); ++s) {
					const std::int32_t t = cavity[s];
					for (int i = 0; i < 3; ++i) {
						const std::uint32_t from = tri(t).v[i];
						const std::uint32_t to	 = tri(t).v[next3(i)];
						if (tri(t).neighbor[i] != kNone) {
							continue; // wired from the other side already
						}
						if (linkBoundary(t, i, from, to)) {
							continue;
						}
						for (std::size_t r = s + 1; r < cavity.size(); ++r) {
							CdtTriangle& y	  = tri(cavity[r]);
							const int	 back = directedEdge(y, to, from);
							if (back < 0) {
								continue;
							}
							tri(t).neighbor[i] = cavity[r];
							y.neighbor[back]   = t;
							std::int32_t tag   = kNone;
							if ((from == a && to == end) || (from == end && to == a)) {
								tag = id;
							}
							for (const BoundaryEdge& h : hanging) {
								if ((h.from == from && h.to == to) || (h.from == to && h.to == from)) {
									tag = h.constraint;
								}
							}
							tri(t).constraint[i] = tag;
							y.constraint[back]	 = tag;
							break;
						}
					}
					for (std::uint32_t v : tri(t).v) {
						vertexTri[v] = t;
					}
				}
			}

			// Hand a new triangle's edge the outside neighbor its cavity edge had.
			bool linkBoundary(std::int32_t t, int i, std::uint32_t from, std::uint32_t to) {
				for (const BoundaryEdge& be : boundary) {
					if (be.from == from && be.to == to) {
						tri(t).neighbor[i]	 = be.outside;
						tri(t).constraint[i] = be.constraint;
						if (be.outside != kNone) {
							tri(be.outside).neighbor[be.outsideEdge] = t;
						}
						return true;
					}
				}
				return false;
			}

			// Anglada's pseudo-polygon triangulation: the chain vertex whose circle
			// through the base holds no other chain vertex makes the base's triangle, and
			// the chain splits at it. The circles through a base form a pencil, so one scan
			// keeping the vertex inside the current best circle finds it.
			void triangulatePseudoPolygon(std::uint32_t a, std::uint32_t b, const std::vector<std::uint32_t>& chain) {
				pending.clear();
				pending.push_back({a, b, 0, chain.size()});
				while (!pending.empty()) {
					const PseudoPolygon pp = pending.back();
					pending.pop_back();
					if (pp.lo == pp.hi) {
						continue;
					}
					std::size_t c = pp.lo;
					for (std::size_t i = pp.lo + 1; i < pp.hi; ++i) {
						if (inCircle(at(pp.a), at(pp.b), at(chain[c]), at(chain[i])) == InCircle::Inside) {
							c = i;
						}
					}
					fresh.push_back({pp.a, pp.b, chain[c]});
					pending.push_back({pp.a, chain[c], pp.lo, c});
					pending.push_back({chain[c], pp.b, c + 1, pp.hi});
				}
			}

			std::vector<Vec2i64>					   vertices;
			std::vector<CdtTriangle>				   tris;
			std::uint32_t							   frameStart = 0;
			std::vector<std::int32_t>				   vertexTri;
			std::vector<std::pair<std::int32_t, int>>  stack;
			std::vector<std::uint32_t>				   mark;
			std::uint32_t							   stamp = 0;
			std::vector<std::int32_t>				   cavity;
			std::vector<std::uint32_t>				   leftChain;
			std::vector<std::uint32_t>				   rightChain;
			std::vector<BoundaryEdge>				   boundary;
			std::vector<BoundaryEdge>				   hanging;
			std::vector<PseudoPolygon>				   pending;
			std::vector<std::array<std::uint32_t, 3>> fresh;
		};

	} // namespace

	ConstrainedDelaunay buildConstrainedDelaunay(
		const std::vector<Vec2i64>& vertices, const std::vector<std::array<std::uint32_t, 2>>& constraints) {
		if (vertices.empty()) {
			return {};
		}
		CdtBuilder builder(vertices);
		builder.insertVertices();
		for (std::size_t c = 0; c < constraints.size(); ++c) {
			const auto [a, b] = constraints[c];
			assert(a < vertices.size() && b < vertices.size() && a != b);
			if (a < vertices.size() && b < vertices.size() && a != b) {
				builder.insertConstraint(static_cast<std::int32_t>(c), a, b);
			}
		}
		builder.restoreDelaunay();
		return builder.finish();
	}

} // namespace geometry
