#include "HalfEdge.h"

#include <algorithm>

namespace geometry {

	HalfEdgeMesh extractFaces(const Arrangement& arrangement) {
		HalfEdgeMesh mesh;
		mesh.vertices = arrangement.vertices;

		// Two directed half-edges per arrangement edge. Even index = from->to,
		// odd index = to->from; they are each other's twin.
		mesh.halfEdges.reserve(arrangement.edges.size() * 2);
		for (std::size_t e = 0; e < arrangement.edges.size(); ++e) {
			const ArrangementEdge& ae = arrangement.edges[e];
			const std::size_t	   h0 = mesh.halfEdges.size();
			HalfEdge			   forward;
			forward.origin = ae.from;
			forward.target = ae.to;
			forward.twin   = h0 + 1;
			forward.edge   = e;
			HalfEdge backward;
			backward.origin = ae.to;
			backward.target = ae.from;
			backward.twin	= h0;
			backward.edge	= e;
			mesh.halfEdges.push_back(forward);
			mesh.halfEdges.push_back(backward);
		}

		// Outgoing half-edges per origin vertex, sorted CCW by direction angle.
		std::vector<std::vector<std::size_t>> outgoing(mesh.vertices.size());
		for (std::size_t h = 0; h < mesh.halfEdges.size(); ++h) {
			outgoing[mesh.halfEdges[h].origin].push_back(h);
		}
		for (std::size_t v = 0; v < outgoing.size(); ++v) {
			std::vector<std::size_t>& list = outgoing[v];
			std::sort(list.begin(), list.end(), [&](std::size_t ha, std::size_t hb) {
				const Vec2i64 da = mesh.vertices[mesh.halfEdges[ha].target] - mesh.vertices[v];
				const Vec2i64 db = mesh.vertices[mesh.halfEdges[hb].target] - mesh.vertices[v];
				return angleLess(da, db);
			});
		}

		// Link next pointers. For a half-edge `h` arriving at vertex v, its twin
		// leaves v; the next half-edge in the face is the one immediately
		// clockwise from that twin around v, i.e. the predecessor in the CCW
		// sorted list (wrapping). This is the standard construction that makes
		// bounded faces wind CCW and the outer boundary wind CW.
		// Half-edge id -> index in its origin's sorted outgoing list. Half-edge
		// ids are dense (0..N-1), so a flat vector keyed by id replaces a map:
		// no allocation per lookup, no O(log N) in this hot link-up.
		std::vector<std::size_t> posInList(mesh.halfEdges.size(), 0);
		for (std::size_t v = 0; v < outgoing.size(); ++v) {
			const std::vector<std::size_t>& list = outgoing[v];
			for (std::size_t k = 0; k < list.size(); ++k) {
				posInList[list[k]] = k;
			}
		}
		for (std::size_t h = 0; h < mesh.halfEdges.size(); ++h) {
			const std::size_t				twin = mesh.halfEdges[h].twin;
			const std::size_t				v	 = mesh.halfEdges[twin].origin; // == target of h
			const std::vector<std::size_t>& list = outgoing[v];
			const std::size_t				k	 = posInList[twin];
			const std::size_t				prev = (k == 0) ? list.size() - 1 : k - 1;
			mesh.halfEdges[h].next				 = list[prev];
		}

		// Walk next-cycles to enumerate faces. Each half-edge belongs to exactly
		// one cycle; mark visited as we go.
		std::vector<bool> visited(mesh.halfEdges.size(), false);
		for (std::size_t start = 0; start < mesh.halfEdges.size(); ++start) {
			if (visited[start]) {
				continue;
			}
			Face				face;
			std::vector<std::int64_t> prov;
			std::size_t			h = start;
			do {
				visited[h] = true;
				mesh.halfEdges[h].face = mesh.faces.size();
				face.halfEdges.push_back(h);
				const std::vector<std::int64_t>& ep = arrangement.edges[mesh.halfEdges[h].edge].provenance;
				prov.insert(prov.end(), ep.begin(), ep.end());
				h = mesh.halfEdges[h].next;
			} while (h != start);

			// Signed doubled area via the shoelace sum in 128-bit, taken about the
			// origin: sum of cross(p_i, p_{i+1}).
			Int128 area2(0);
			for (std::size_t idx = 0; idx < face.halfEdges.size(); ++idx) {
				const HalfEdge& he = mesh.halfEdges[face.halfEdges[idx]];
				area2 = area2 + cross(mesh.vertices[he.origin], mesh.vertices[he.target]);
			}
			face.signedAreaDoubled = area2;
			face.outer			   = area2.sign() < 0;

			std::sort(prov.begin(), prov.end());
			prov.erase(std::unique(prov.begin(), prov.end()), prov.end());
			face.provenance = std::move(prov);

			mesh.faces.push_back(std::move(face));
		}

		// Representative interior point for each bounded face. A convex corner of
		// the cycle plus a nudge toward the centroid is not exact on the grid; we
		// instead take, for each consecutive vertex triple that turns left, the
		// rounded centroid of that ear triangle and accept the first one the exact
		// containment query confirms is strictly inside. For the integer rings
		// this arrangement produces (no zero-length edges, real positive area) at
		// least one ear centroid lands strictly inside.
		for (Face& face : mesh.faces) {
			if (face.outer) {
				continue;
			}
			std::vector<Vec2i64> ring;
			ring.reserve(face.halfEdges.size());
			for (std::size_t he : face.halfEdges) {
				ring.push_back(mesh.vertices[mesh.halfEdges[he].origin]);
			}
			const std::size_t n = ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64& a = ring[i];
				const Vec2i64& b = ring[(i + 1) % n];
				const Vec2i64& c = ring[(i + 2) % n];
				if (orientation(a, b, c) != Orientation::CounterClockwise) {
					continue; // not a left turn; skip reflex/straight corners
				}
				const Vec2i64 centroid{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3};
				if (pointInPolygon(centroid, ring) == PointInPolygon::Inside) {
					face.representativePoint = centroid;
					break;
				}
			}
		}

		return mesh;
	}

	std::vector<std::size_t> HalfEdgeMesh::faceEdges(std::size_t faceIndex) const {
		std::vector<std::size_t> result;
		const Face&				 face = faces[faceIndex];
		result.reserve(face.halfEdges.size());
		for (std::size_t he : face.halfEdges) {
			result.push_back(halfEdges[he].edge);
		}
		return result;
	}

	std::size_t HalfEdgeMesh::twinFace(std::size_t halfEdgeIndex) const {
		return halfEdges[halfEdges[halfEdgeIndex].twin].face;
	}

	std::vector<std::size_t> HalfEdgeMesh::facesAtVertex(std::size_t vertexIndex) const {
		std::vector<std::size_t> result;
		for (const HalfEdge& he : halfEdges) {
			if (he.origin == vertexIndex) {
				result.push_back(he.face);
			}
		}
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

	PointInPolygon pointInCycle(const Vec2i64& point, const HalfEdgeMesh& mesh, std::size_t faceIndex) {
		const Face&			 face = mesh.faces[faceIndex];
		std::vector<Vec2i64> ring;
		ring.reserve(face.halfEdges.size());
		for (std::size_t he : face.halfEdges) {
			ring.push_back(mesh.vertices[mesh.halfEdges[he].origin]);
		}
		return pointInPolygon(point, ring);
	}

} // namespace geometry
