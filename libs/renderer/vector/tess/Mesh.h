#pragma once

// Half-edge (DCEL) mesh for robust polygon tessellation.
//
// Ported to modern C++ from libtess2 (mesh.h / mesh.c) -- the SGI GLU tessellator
// by Eric Veach. Original under the SGI Free Software License B 2.0
// (http://oss.sgi.com/projects/FreeB/). Adapted to our conventions: RAII node
// pools instead of bucket allocators, accessor methods instead of the
// Sym->Lnext navigation macros, and 2D only (s,t hold the vertex position; there
// is no 3D coordinate or sweep-plane projection).
//
// Two half-edges make one edge, pointing opposite ways. Navigation:
//   sym   = mate (same edge, opposite direction)
//   onext = next edge CCW around the origin vertex
//   lnext = next edge CCW around the left face
// "prev"/"dst"/"rface" etc. are derived (see the accessor methods).

#include <deque>

namespace renderer::tess {

	struct Vertex;
	struct HalfEdge;
	struct Face;
	struct ActiveRegion; // defined by the sweep

	struct HalfEdge {
		HalfEdge* next{nullptr};  // global edge list (visits one half-edge per edge)
		HalfEdge* sym{nullptr};   // same edge, opposite direction
		HalfEdge* onext{nullptr}; // next edge CCW around origin
		HalfEdge* lnext{nullptr}; // next edge CCW around left face
		Vertex*	  org{nullptr};	  // origin vertex
		Face*	  lface{nullptr}; // left face

		ActiveRegion* activeRegion{nullptr}; // sweep: region whose upper edge is this
		int			  winding{0};			 // winding-number delta crossing right->left face

		Vertex*	  dst() const { return sym->org; }
		Face*	  rface() const { return sym->lface; }
		HalfEdge* oprev() const { return sym->lnext; }
		HalfEdge* lprev() const { return onext->sym; }
		HalfEdge* dprev() const { return lnext->sym; }
		HalfEdge* rprev() const { return sym->onext; }
		HalfEdge* dnext() const { return rprev()->sym; }
		HalfEdge* rnext() const { return oprev()->sym; }
	};

	struct Vertex {
		Vertex*	  next{nullptr};   // next vertex in the global list (never null)
		Vertex*	  prev{nullptr};   // previous vertex (never null)
		HalfEdge* anEdge{nullptr}; // a half-edge with this origin
		float	  s{0.0F};		   // sweep-plane coordinates == the 2D position
		float	  t{0.0F};
		int		  pqHandle{0};	   // priority-queue handle (sweep), for deletion
		int		  idx{-1};		   // output index, assigned at triangulation
	};

	struct Face {
		Face*	  next{nullptr};   // next face in the global list (never null)
		Face*	  prev{nullptr};   // previous face (never null)
		HalfEdge* anEdge{nullptr}; // a half-edge with this as its left face
		Face*	  trail{nullptr};  // stack link used by monotone-region triangulation
		bool	  marked{false};   // monotone-conversion flag
		bool	  inside{false};   // face lies in the polygon interior
	};

	// Half-edges are allocated two at a time. Keeping a pair in one struct (e first,
	// eSym second) makes the within-struct address order well-defined, so the
	// canonical-of-pair test (e->sym < e) used by the edge-list bookkeeping is exact.
	struct EdgePair {
		HalfEdge e;
		HalfEdge eSym;
	};

	// Owns every mesh node (RAII). Inter-node pointers stay valid because the pools
	// (std::deque) never relocate existing elements on growth.
	class Mesh {
	  public:
		Mesh();

		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;
		Mesh(Mesh&&) = delete;
		Mesh& operator=(Mesh&&) = delete;
		~Mesh() = default;

		// Dummy list-header sentinels. Walk vertices via vHead.next until back at
		// &vHead; likewise faces (fHead) and edges (eHead, one half-edge per edge).
		Vertex	 vHead{};
		Face	 fHead{};
		HalfEdge eHead{};
		HalfEdge eHeadSym{};

		// Basic Euler operations (everything else is built from these).
		// makeEdge: one edge, two vertices, and a single loop (face).
		HalfEdge* makeEdge();
		// splice: the fundamental connectivity operation (merges/splits vertices and faces).
		bool	  splice(HalfEdge* eOrg, HalfEdge* eDst);
		// deleteEdge: remove eDel, joining or splitting the incident loops.
		bool	  deleteEdge(HalfEdge* eDel);

		// Convenience operations.
		// addEdgeVertex: new edge eNew == eOrg->lnext with a fresh destination vertex.
		HalfEdge* addEdgeVertex(HalfEdge* eOrg);
		// splitEdge: split eOrg into eOrg + eNew (== eOrg->lnext) sharing a new vertex.
		HalfEdge* splitEdge(HalfEdge* eOrg);
		// connect: new edge from eOrg->dst to eDst->org; splits or merges loops.
		HalfEdge* connect(HalfEdge* eOrg, HalfEdge* eDst);

		// Remove a face from the global list, nulling its edges' left face; deletes
		// any edge left with no interior face (and any isolated vertices produced).
		void zapFace(Face* fZap);

		// Validate the half-edge invariants across all lists. Debug/test aid; returns
		// false on the first inconsistency.
		bool checkConsistency() const;

	  private:
		// Allocate the two half-edges of a new edge (e and e->sym) and return e.
		HalfEdge* makeEdgePair(HalfEdge* eNext);
		// Attach a new origin vertex to every half-edge in eOrig's origin orbit,
		// inserting it before vNext in the vertex list.
		void	  makeVertex(HalfEdge* eOrig, Vertex* vNext);
		// Attach a new left face to every half-edge in eOrig's left-face orbit,
		// inserting it before fNext in the face list.
		void	  makeFace(HalfEdge* eOrig, Face* fNext);
		void	  killEdge(HalfEdge* eDel);
		void	  killVertex(Vertex* vDel, Vertex* newOrg);
		void	  killFace(Face* fDel, Face* newLface);

		std::deque<Vertex>	 vertexPool;
		std::deque<Face>	 facePool;
		std::deque<EdgePair> edgePool;
	};

} // namespace renderer::tess
