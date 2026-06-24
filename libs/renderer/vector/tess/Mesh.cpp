#include "Mesh.h"

// Euler operators for the half-edge mesh. Ported to modern C++ from libtess2
// (mesh.c) -- the SGI GLU tessellator by Eric Veach, SGI Free Software License B
// 2.0 (http://oss.sgi.com/projects/FreeB/). Differences from the original:
//   - RAII std::deque pools; kill* unlink nodes but do not reclaim storage (the
//     pools free everything when the Mesh is destroyed), so there is no free list.
//   - Edges are pooled as EdgePair structs so the canonical-of-pair test is exact.
//   - Navigation uses the HalfEdge accessor methods instead of the C macros.

namespace renderer::tess {

	namespace {
		// Splice( a, b ): exchange a->onext and b->onext. Depending on whether a and b
		// share a vertex/face ring this merges or splits those rings. See mesh.h.
		void splicePrim(HalfEdge* a, HalfEdge* b) {
			HalfEdge* aOnext = a->onext;
			HalfEdge* bOnext = b->onext;
			aOnext->sym->lnext = b;
			bOnext->sym->lnext = a;
			a->onext = bOnext;
			b->onext = aOnext;
		}
	} // namespace

	Mesh::Mesh() {
		// Empty mesh: each dummy header is its own circular list, and the edge header
		// pair points at each other. e->next == e means "no edges yet".
		vHead.next = &vHead;
		vHead.prev = &vHead;

		fHead.next = &fHead;
		fHead.prev = &fHead;

		eHead.next = &eHead;
		eHead.sym = &eHeadSym;

		eHeadSym.next = &eHeadSym;
		eHeadSym.sym = &eHead;
	}

	HalfEdge* Mesh::makeEdgePair(HalfEdge* eNext) {
		EdgePair& pair = edgePool.emplace_back();
		HalfEdge* e = &pair.e;
		HalfEdge* eSym = &pair.eSym;

		// Make sure eNext points to the first (canonical) half-edge of its pair.
		if (eNext->sym < eNext) {
			eNext = eNext->sym;
		}

		// Insert into the circular doubly-linked edge list before eNext. A half-edge's
		// "prev" link lives in sym->next (only one direction is stored per pair).
		HalfEdge* ePrev = eNext->sym->next;
		eSym->next = ePrev;
		ePrev->sym->next = e;
		e->next = eNext;
		eNext->sym->next = eSym;

		e->sym = eSym;
		e->onext = e;
		e->lnext = eSym;

		eSym->sym = e;
		eSym->onext = eSym;
		eSym->lnext = e;

		return e;
	}

	void Mesh::makeVertex(HalfEdge* eOrig, Vertex* vNext) {
		Vertex& vNew = vertexPool.emplace_back();

		// Insert before vNext so list walks don't see freshly created vertices.
		Vertex* vPrev = vNext->prev;
		vNew.prev = vPrev;
		vPrev->next = &vNew;
		vNew.next = vNext;
		vNext->prev = &vNew;

		vNew.anEdge = eOrig;

		// Make this the origin of every edge in eOrig's origin orbit.
		HalfEdge* e = eOrig;
		do {
			e->org = &vNew;
			e = e->onext;
		} while (e != eOrig);
	}

	void Mesh::makeFace(HalfEdge* eOrig, Face* fNext) {
		Face& fNew = facePool.emplace_back();

		Face* fPrev = fNext->prev;
		fNew.prev = fPrev;
		fPrev->next = &fNew;
		fNew.next = fNext;
		fNext->prev = &fNew;

		fNew.anEdge = eOrig;
		fNew.trail = nullptr;
		fNew.marked = false;
		fNew.inside = fNext->inside; // a split face inherits the interior flag

		HalfEdge* e = eOrig;
		do {
			e->lface = &fNew;
			e = e->lnext;
		} while (e != eOrig);
	}

	void Mesh::killEdge(HalfEdge* eDel) {
		// Operate on the canonical half-edge of the pair.
		if (eDel->sym < eDel) {
			eDel = eDel->sym;
		}
		HalfEdge* eNext = eDel->next;
		HalfEdge* ePrev = eDel->sym->next;
		eNext->sym->next = ePrev;
		ePrev->sym->next = eNext;
		// Storage is not reclaimed; the pool frees it with the mesh.
	}

	void Mesh::killVertex(Vertex* vDel, Vertex* newOrg) {
		// Repoint every edge in vDel's origin orbit at newOrg (may be null).
		HalfEdge* eStart = vDel->anEdge;
		HalfEdge* e = eStart;
		do {
			e->org = newOrg;
			e = e->onext;
		} while (e != eStart);

		Vertex* vPrev = vDel->prev;
		Vertex* vNext = vDel->next;
		vNext->prev = vPrev;
		vPrev->next = vNext;
	}

	void Mesh::killFace(Face* fDel, Face* newLface) {
		HalfEdge* eStart = fDel->anEdge;
		HalfEdge* e = eStart;
		do {
			e->lface = newLface;
			e = e->lnext;
		} while (e != eStart);

		Face* fPrev = fDel->prev;
		Face* fNext = fDel->next;
		fNext->prev = fPrev;
		fPrev->next = fNext;
	}

	HalfEdge* Mesh::makeEdge() {
		HalfEdge* e = makeEdgePair(&eHead);
		makeVertex(e, &vHead);
		makeVertex(e->sym, &vHead);
		makeFace(e, &fHead);
		return e;
	}

	bool Mesh::splice(HalfEdge* eOrg, HalfEdge* eDst) {
		bool joiningLoops = false;
		bool joiningVertices = false;

		if (eOrg == eDst) {
			return true;
		}

		if (eDst->org != eOrg->org) {
			joiningVertices = true; // merging two vertices -- destroy eDst->org
			killVertex(eDst->org, eOrg->org);
		}
		if (eDst->lface != eOrg->lface) {
			joiningLoops = true; // joining two loops -- destroy eDst->lface
			killFace(eDst->lface, eOrg->lface);
		}

		splicePrim(eDst, eOrg);

		if (!joiningVertices) {
			// Split one vertex into two; the new vertex is eDst->org.
			makeVertex(eDst, eOrg->org);
			eOrg->org->anEdge = eOrg;
		}
		if (!joiningLoops) {
			// Split one loop into two; the new loop is eDst->lface.
			makeFace(eDst, eOrg->lface);
			eOrg->lface->anEdge = eOrg;
		}

		return true;
	}

	bool Mesh::deleteEdge(HalfEdge* eDel) {
		HalfEdge* eDelSym = eDel->sym;
		bool	  joiningLoops = false;

		// Disconnect the origin vertex, keeping the mesh consistent throughout.
		if (eDel->lface != eDel->rface()) {
			joiningLoops = true; // joining two loops -- remove the left face
			killFace(eDel->lface, eDel->rface());
		}

		if (eDel->onext == eDel) {
			killVertex(eDel->org, nullptr);
		} else {
			eDel->rface()->anEdge = eDel->oprev();
			eDel->org->anEdge = eDel->onext;
			splicePrim(eDel, eDel->oprev());
			if (!joiningLoops) {
				makeFace(eDel, eDel->lface); // split one loop into two
			}
		}

		// Now disconnect eDel->dst (eDel->org may already be gone).
		if (eDelSym->onext == eDelSym) {
			killVertex(eDelSym->org, nullptr);
			killFace(eDelSym->lface, nullptr);
		} else {
			eDel->lface->anEdge = eDelSym->oprev();
			eDelSym->org->anEdge = eDelSym->onext;
			splicePrim(eDelSym, eDelSym->oprev());
		}

		killEdge(eDel);
		return true;
	}

	HalfEdge* Mesh::addEdgeVertex(HalfEdge* eOrg) {
		HalfEdge* eNew = makeEdgePair(eOrg);
		HalfEdge* eNewSym = eNew->sym;

		splicePrim(eNew, eOrg->lnext);

		eNew->org = eOrg->dst();
		makeVertex(eNewSym, eNew->org); // eNewSym's origin is the fresh vertex
		eNew->lface = eNewSym->lface = eOrg->lface;

		return eNew;
	}

	HalfEdge* Mesh::splitEdge(HalfEdge* eOrg) {
		HalfEdge* tempHalfEdge = addEdgeVertex(eOrg);
		HalfEdge* eNew = tempHalfEdge->sym;

		// Disconnect eOrg from its destination and reconnect it to eNew->org.
		splicePrim(eOrg->sym, eOrg->sym->oprev());
		splicePrim(eOrg->sym, eNew);

		eOrg->sym->org = eNew->org;			 // eOrg->dst == eNew->org
		eNew->dst()->anEdge = eNew->sym;	 // may have pointed at eOrg->sym
		eNew->sym->lface = eOrg->sym->lface; // eNew->rface = eOrg->rface
		eNew->winding = eOrg->winding;		 // copy winding across the split
		eNew->sym->winding = eOrg->sym->winding;

		return eNew;
	}

	HalfEdge* Mesh::connect(HalfEdge* eOrg, HalfEdge* eDst) {
		bool	  joiningLoops = false;
		HalfEdge* eNew = makeEdgePair(eOrg);
		HalfEdge* eNewSym = eNew->sym;

		if (eDst->lface != eOrg->lface) {
			joiningLoops = true; // merging two loops -- destroy eDst->lface
			killFace(eDst->lface, eOrg->lface);
		}

		splicePrim(eNew, eOrg->lnext);
		splicePrim(eNewSym, eDst);

		eNew->org = eOrg->dst();
		eNewSym->org = eDst->org;
		eNew->lface = eNewSym->lface = eOrg->lface;

		eOrg->lface->anEdge = eNewSym;

		if (!joiningLoops) {
			makeFace(eNew, eOrg->lface); // split one loop into two; new loop is eNew->lface
		}
		return eNew;
	}

	void Mesh::zapFace(Face* fZap) {
		HalfEdge* eStart = fZap->anEdge;

		// Walk the face, deleting any edge whose right face is also null.
		HalfEdge* eNext = eStart->lnext;
		HalfEdge* e = nullptr;
		do {
			e = eNext;
			eNext = e->lnext;

			e->lface = nullptr;
			if (e->rface() == nullptr) {
				if (e->onext == e) {
					killVertex(e->org, nullptr);
				} else {
					e->org->anEdge = e->onext;
					splicePrim(e, e->oprev());
				}
				HalfEdge* eSym = e->sym;
				if (eSym->onext == eSym) {
					killVertex(eSym->org, nullptr);
				} else {
					eSym->org->anEdge = eSym->onext;
					splicePrim(eSym, eSym->oprev());
				}
				killEdge(e);
			}
		} while (e != eStart);

		Face* fPrev = fZap->prev;
		Face* fNext = fZap->next;
		fNext->prev = fPrev;
		fPrev->next = fNext;
	}

	bool Mesh::checkConsistency() const {
		const Face*		fHeadPtr = &fHead;
		const Vertex*	vHeadPtr = &vHead;
		const HalfEdge* eHeadPtr = &eHead;

		const Face* fPrev = fHeadPtr;
		for (const Face* f = fPrev->next; f != fHeadPtr; fPrev = f, f = f->next) {
			if (f->prev != fPrev) {
				return false;
			}
			const HalfEdge* e = f->anEdge;
			do {
				if (e->sym == e || e->sym->sym != e) {
					return false;
				}
				if (e->lnext->onext->sym != e || e->onext->sym->lnext != e) {
					return false;
				}
				if (e->lface != f) {
					return false;
				}
				e = e->lnext;
			} while (e != f->anEdge);
		}

		const Vertex* vPrev = vHeadPtr;
		for (const Vertex* v = vPrev->next; v != vHeadPtr; vPrev = v, v = v->next) {
			if (v->prev != vPrev) {
				return false;
			}
			const HalfEdge* e = v->anEdge;
			do {
				if (e->sym == e || e->sym->sym != e) {
					return false;
				}
				if (e->lnext->onext->sym != e || e->onext->sym->lnext != e) {
					return false;
				}
				if (e->org != v) {
					return false;
				}
				e = e->onext;
			} while (e != v->anEdge);
		}

		const HalfEdge* ePrev = eHeadPtr;
		for (const HalfEdge* e = ePrev->next; e != eHeadPtr; ePrev = e, e = e->next) {
			if (e->sym->next != ePrev->sym) {
				return false;
			}
			if (e->sym == e || e->sym->sym != e) {
				return false;
			}
			if (e->org == nullptr || e->dst() == nullptr) {
				return false;
			}
			if (e->lnext->onext->sym != e || e->onext->sym->lnext != e) {
				return false;
			}
		}

		return true;
	}

} // namespace renderer::tess
