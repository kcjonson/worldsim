#include "Tessellator.h"

#include "tess/Mesh.h"
#include "tess/Sweep.h"
#include "tess/Triangulate.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace renderer {

	namespace {

		constexpr size_t kMaxOutputVertices = 65535; // index type is uint16_t

		// Convex test: all edge cross products share a sign. Convex shapes can use an O(n) fan.
		bool isConvexPolygon(const std::vector<Foundation::Vec2>& vertices) {
			if (vertices.size() < 3) {
				return false;
			}
			int			 sign = 0;
			const size_t n = vertices.size();
			for (size_t i = 0; i < n; ++i) {
				const auto&		 p0 = vertices[i];
				const auto&		 p1 = vertices[(i + 1) % n];
				const auto&		 p2 = vertices[(i + 2) % n];
				Foundation::Vec2 edge1 = p1 - p0;
				Foundation::Vec2 edge2 = p2 - p1;
				const float		 cross = (edge1.x * edge2.y) - (edge1.y * edge2.x);
				if (std::abs(cross) < 1e-6F) {
					continue; // collinear
				}
				const int currentSign = (cross > 0) ? 1 : -1;
				if (sign == 0) {
					sign = currentSign;
				} else if (sign != currentSign) {
					return false;
				}
			}
			return true;
		}

		// Fan from an inserted centroid: one interior sample point so per-vertex gradient fills
		// can render a center. Convex (or star-convex) only.
		void tessellateCentroidFan(const std::vector<Foundation::Vec2>& vertices, TessellatedMesh& outMesh) {
			const size_t n = vertices.size();
			// Polygon centroid: an approximation of a radial gradient's center (exact only when
			// the gradient origin sits at the centroid). Good enough for the symmetric fills this
			// path serves; off-center radials get an interpolated, not placed, bright spot.
			Foundation::Vec2 centroid{0.0F, 0.0F};
			for (const auto& v : vertices) {
				centroid.x += v.x;
				centroid.y += v.y;
			}
			centroid.x /= static_cast<float>(n);
			centroid.y /= static_cast<float>(n);

			outMesh.vertices.reserve(n + 1);
			outMesh.vertices.push_back(centroid);
			for (const auto& v : vertices) {
				outMesh.vertices.push_back(v);
			}
			outMesh.indices.reserve(n * 3);
			for (size_t i = 0; i < n; ++i) {
				outMesh.indices.push_back(0);
				outMesh.indices.push_back(static_cast<uint16_t>(1 + i));
				outMesh.indices.push_back(static_cast<uint16_t>(1 + ((i + 1) % n)));
			}
		}

		// Fan from vertex 0. O(n), convex only.
		void tessellateConvexFan(const std::vector<Foundation::Vec2>& vertices, TessellatedMesh& outMesh) {
			outMesh.vertices = vertices;
			outMesh.indices.reserve((vertices.size() - 2) * 3);
			for (size_t i = 1; i + 1 < vertices.size(); ++i) {
				outMesh.indices.push_back(0);
				outMesh.indices.push_back(static_cast<uint16_t>(i));
				outMesh.indices.push_back(static_cast<uint16_t>(i + 1));
			}
		}

		// Net signed area (shoelace) over all contours. Sign tells overall orientation.
		double netSignedArea(std::span<const VectorPath> contours) {
			double area = 0.0;
			for (const auto& c : contours) {
				const auto&	 vs = c.vertices;
				const size_t n = vs.size();
				if (n < 3) {
					continue;
				}
				for (size_t i = 0; i < n; ++i) {
					const auto& a = vs[i];
					const auto& b = vs[(i + 1) % n];
					area += (static_cast<double>(a.x) * b.y) - (static_cast<double>(b.x) * a.y);
				}
			}
			return area;
		}

		// Sweep-line tessellation of one or more contours. Handles concavity, holes, and
		// self-intersection; resolves the interior by winding rule and triangulates it.
		bool sweepTessellate(std::span<const VectorPath> contours, TessellatedMesh& outMesh,
							 const TessellatorOptions& options) {
			using namespace renderer::tess;

			// libtess assumes inside faces wind CCW in sweep space; flip t when the input's net
			// orientation is clockwise, then un-flip y on output.
			const bool flipT = netSignedArea(contours) < 0.0;

			Mesh mesh;
			bool built = false;
			for (const auto& c : contours) {
				const auto& vs = c.vertices;
				if (vs.size() < 3) {
					continue;
				}
				HalfEdge* e = nullptr;
				for (const auto& p : vs) {
					if (e == nullptr) {
						e = mesh.makeEdge();
						mesh.splice(e, e->sym);
					} else {
						mesh.splitEdge(e);
						e = e->lnext;
					}
					e->org->s = p.x;
					e->org->t = flipT ? -p.y : p.y;
					e->winding = 1;
					e->sym->winding = -1;
				}
				built = true;
			}
			if (!built) {
				LOG_ERROR(Renderer, "Tessellator: no contour with >= 3 vertices");
				return false;
			}

			// Bounds in sweep space, for the sentinel edges.
			float bmin[2] = {0.0F, 0.0F};
			float bmax[2] = {0.0F, 0.0F};
			bool  first = true;
			for (Vertex* v = mesh.vHead.next; v != &mesh.vHead; v = v->next) {
				if (first) {
					bmin[0] = bmax[0] = v->s;
					bmin[1] = bmax[1] = v->t;
					first = false;
				} else {
					bmin[0] = std::min(bmin[0], v->s);
					bmax[0] = std::max(bmax[0], v->s);
					bmin[1] = std::min(bmin[1], v->t);
					bmax[1] = std::max(bmax[1], v->t);
				}
			}

			const WindingRule rule =
				options.useNonZeroFillRule ? WindingRule::NonZero : WindingRule::Odd;
			Sweep sweep(mesh, rule, bmin, bmax);
			sweep.computeInterior();
			tessellateInterior(mesh);

			// Emit unique vertices used by interior faces, then triangle indices.
			outMesh.clear();
			for (Vertex* v = mesh.vHead.next; v != &mesh.vHead; v = v->next) {
				v->idx = -1;
			}
			for (Face* f = mesh.fHead.next; f != &mesh.fHead; f = f->next) {
				if (!f->inside) {
					continue;
				}
				HalfEdge* e = f->anEdge;
				do {
					Vertex* v = e->org;
					if (v->idx < 0) {
						v->idx = static_cast<int>(outMesh.vertices.size());
						outMesh.vertices.push_back(Foundation::Vec2{v->s, flipT ? -v->t : v->t});
					}
					e = e->lnext;
				} while (e != f->anEdge);
			}

			if (outMesh.vertices.size() > kMaxOutputVertices) {
				LOG_ERROR(Renderer, "Tessellator: output exceeds %zu vertices (%zu)", kMaxOutputVertices,
						  outMesh.vertices.size());
				outMesh.clear();
				return false;
			}

			for (Face* f = mesh.fHead.next; f != &mesh.fHead; f = f->next) {
				if (!f->inside) {
					continue;
				}
				HalfEdge*	   e = f->anEdge;
				const uint16_t i0 = static_cast<uint16_t>(e->org->idx);
				HalfEdge*	   e1 = e->lnext;
				HalfEdge*	   e2 = e1->lnext;
				for (; e2 != f->anEdge; e1 = e2, e2 = e2->lnext) {
					outMesh.indices.push_back(i0);
					outMesh.indices.push_back(static_cast<uint16_t>(e1->org->idx));
					outMesh.indices.push_back(static_cast<uint16_t>(e2->org->idx));
				}
			}
			return true;
		}

	} // namespace

	bool Tessellator::Tessellate(const VectorPath& path, TessellatedMesh& outMesh,
								 const TessellatorOptions& options) {
		outMesh.clear();

		if (path.vertices.size() < 3) {
			LOG_ERROR(Renderer, "Tessellator: Path must have at least 3 vertices");
			return false;
		}
		// Output indices are 16-bit; cap the input here so all three paths (both fans and the
		// sweep) are covered. The sweep can still add intersection vertices, so it keeps its own
		// post-tessellation backstop.
		if (path.vertices.size() > kMaxOutputVertices) {
			LOG_ERROR(Renderer, "Tessellator: input exceeds %zu vertices (%zu)", kMaxOutputVertices,
					  path.vertices.size());
			return false;
		}
		if (!path.isClosed) {
			LOG_WARNING(Renderer, "Tessellator: Path is not closed, closing it automatically");
		}

		// Gradient fills want an interior sample point: fan from an inserted centroid (convex only).
		if (options.fanFromCentroid && isConvexPolygon(path.vertices)) {
			tessellateCentroidFan(path.vertices, outMesh);
			return true;
		}
		// Convex fast path (circles, ellipses, simple shapes): O(n) fan.
		if (isConvexPolygon(path.vertices)) {
			tessellateConvexFan(path.vertices, outMesh);
			return true;
		}
		// Concave / self-intersecting: sweep-line over a single contour.
		return sweepTessellate(std::span<const VectorPath>(&path, 1), outMesh, options);
	}

	bool Tessellator::Tessellate(std::span<const VectorPath> contours, TessellatedMesh& outMesh,
								 const TessellatorOptions& options) {
		outMesh.clear();
		if (contours.size() == 1) {
			// Single contour can use the convex fast path.
			return Tessellate(contours[0], outMesh, options);
		}
		if (contours.empty()) {
			LOG_ERROR(Renderer, "Tessellator: no contours");
			return false;
		}
		return sweepTessellate(contours, outMesh, options);
	}

} // namespace renderer
