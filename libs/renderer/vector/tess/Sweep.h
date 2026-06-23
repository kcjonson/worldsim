#pragma once

// Sweep-line construction of the planar arrangement, with interior regions
// marked per winding rule and left monotone. Ported to modern C++ from libtess2
// (sweep.c / sweep.h) -- the SGI GLU tessellator by Eric Veach, SGI Free Software
// License B 2.0 (http://oss.sgi.com/projects/FreeB/).
//
// Differences from the original: 2D only (no 3D normal/projection; s,t are the
// position); only the ODD and NONZERO winding rules; no user-data coordinate
// interpolation at intersections; std-backed priority queue and region pool; the
// setjmp/longjmp out-of-memory escape is gone (mesh ops always succeed here).

#include "Dict.h"
#include "Mesh.h"
#include "PriorityQueue.h"

#include <deque>
#include <memory>

namespace renderer::tess {

	enum class WindingRule {
		Odd,	 // SVG even-odd
		NonZero, // SVG nonzero (default)
	};

	// The region between a pair of adjacent edges crossing the sweep line.
	struct ActiveRegion {
		HalfEdge* eUp{nullptr};		   // upper edge, directed right to left
		DictNode* nodeUp{nullptr};	   // dictionary node for eUp
		int		  windingNumber{0};	   // winding number of this region
		bool	  inside{false};	   // is this region inside the polygon?
		bool	  sentinel{false};	   // a fake edge at t = +/- infinity
		bool	  dirty{false};		   // an edge changed; intersection not yet checked
		bool	  fixUpperEdge{false}; // a temporary edge from connectRightVertex
	};

	class Sweep {
	  public:
		Sweep(Mesh& tessMesh, WindingRule rule, const float boundsMin[2], const float boundsMax[2]);

		Sweep(const Sweep&) = delete;
		Sweep& operator=(const Sweep&) = delete;

		// Build the arrangement and mark interior monotone regions. The event vertex
		// is read by the dictionary comparator during this call.
		void computeInterior();

	  private:
		static bool edgeLeqThunk(void* frame, ActiveRegion* reg1, ActiveRegion* reg2);
		bool		edgeLeq(ActiveRegion* reg1, ActiveRegion* reg2) const;

		ActiveRegion* regionBelow(ActiveRegion* r) const { return dictKey(dictPred(r->nodeUp)); }
		ActiveRegion* regionAbove(ActiveRegion* r) const { return dictKey(dictSucc(r->nodeUp)); }

		ActiveRegion* allocRegion();
		void		  deleteRegion(ActiveRegion* reg);
		void		  replaceUpperEdge(ActiveRegion* reg, HalfEdge* newEdge);
		ActiveRegion* topLeftRegion(ActiveRegion* reg);
		ActiveRegion* topRightRegion(ActiveRegion* reg);
		ActiveRegion* addRegionBelow(ActiveRegion* regAbove, HalfEdge* eNewUp);
		bool		  isWindingInside(int n) const;
		void		  computeWinding(ActiveRegion* reg);
		void		  finishRegion(ActiveRegion* reg);
		HalfEdge*	  finishLeftRegions(ActiveRegion* regFirst, ActiveRegion* regLast);
		void addRightEdges(ActiveRegion* regUp, HalfEdge* eFirst, HalfEdge* eLast, HalfEdge* eTopLeft,
						   bool cleanUp);
		void spliceMergeVertices(HalfEdge* e1, HalfEdge* e2);
		bool checkForRightSplice(ActiveRegion* regUp);
		bool checkForLeftSplice(ActiveRegion* regUp);
		bool checkForIntersect(ActiveRegion* regUp);
		void walkDirtyRegions(ActiveRegion* regUp);
		void connectRightVertex(ActiveRegion* regUp, HalfEdge* eBottomLeft);
		void connectLeftDegenerate(ActiveRegion* regUp, Vertex* vEvent);
		void connectLeftVertex(Vertex* vEvent);
		void sweepEvent(Vertex* vEvent);
		void addSentinel(float smin, float smax, float t);
		void initEdgeDict();
		void doneEdgeDict();
		void removeDegenerateEdges();
		void initPriorityQ();
		void removeDegenerateFaces();

		Mesh&					 mesh;
		WindingRule				 windingRule;
		Vertex*					 event{nullptr};
		std::unique_ptr<Dict>	 dict;
		PriorityQueue			 pq;
		std::deque<ActiveRegion> regionPool;
		float					 bmin[2]{0.0F, 0.0F};
		float					 bmax[2]{0.0F, 0.0F};
	};

} // namespace renderer::tess
