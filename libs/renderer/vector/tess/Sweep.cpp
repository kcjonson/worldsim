#include "Sweep.h"

#include "Predicates.h"

#include <algorithm>
#include <cassert>
#include <cfloat>

// Ported from libtess2 sweep.c (SGI Free Software License B 2.0). The dictionary
// invariants and the per-function comments in the original explain the algorithm;
// this is a faithful translation to our mesh/types with the simplifications noted
// in Sweep.h.

namespace renderer::tess {

	namespace {
		// Combined winding when two edges merge into one.
		void addWinding(HalfEdge* eDst, HalfEdge* eSrc) {
			eDst->winding += eSrc->winding;
			eDst->sym->winding += eSrc->sym->winding;
		}
	} // namespace

	Sweep::Sweep(Mesh& tessMesh, WindingRule rule, const float boundsMin[2], const float boundsMax[2])
		: mesh(tessMesh), windingRule(rule) {
		bmin[0] = boundsMin[0];
		bmin[1] = boundsMin[1];
		bmax[0] = boundsMax[0];
		bmax[1] = boundsMax[1];
	}

	bool Sweep::edgeLeqThunk(void* frame, ActiveRegion* reg1, ActiveRegion* reg2) {
		return static_cast<Sweep*>(frame)->edgeLeq(reg1, reg2);
	}

	bool Sweep::edgeLeq(ActiveRegion* reg1, ActiveRegion* reg2) const {
		// Both edges are directed right to left. Evaluate a t value for each at the
		// current sweep position (event); if both destinations are at the event, sort
		// by slope.
		HalfEdge* e1 = reg1->eUp;
		HalfEdge* e2 = reg2->eUp;

		if (e1->dst() == event) {
			if (e2->dst() == event) {
				if (vertLeq(*e1->org, *e2->org)) {
					return edgeSign(*e2->dst(), *e1->org, *e2->org) <= 0.0F;
				}
				return edgeSign(*e1->dst(), *e2->org, *e1->org) >= 0.0F;
			}
			return edgeSign(*e2->dst(), *event, *e2->org) <= 0.0F;
		}
		if (e2->dst() == event) {
			return edgeSign(*e1->dst(), *event, *e1->org) >= 0.0F;
		}
		const float t1 = edgeEval(*e1->dst(), *event, *e1->org);
		const float t2 = edgeEval(*e2->dst(), *event, *e2->org);
		return t1 >= t2;
	}

	ActiveRegion* Sweep::allocRegion() { return &regionPool.emplace_back(); }

	void Sweep::deleteRegion(ActiveRegion* reg) {
		if (reg->fixUpperEdge) {
			// Created with zero winding, must be deleted with zero winding.
			assert(reg->eUp->winding == 0);
		}
		reg->eUp->activeRegion = nullptr;
		dict->remove(reg->nodeUp);
	}

	void Sweep::replaceUpperEdge(ActiveRegion* reg, HalfEdge* newEdge) {
		assert(reg->fixUpperEdge);
		mesh.deleteEdge(reg->eUp);
		reg->fixUpperEdge = false;
		reg->eUp = newEdge;
		newEdge->activeRegion = reg;
	}

	ActiveRegion* Sweep::topLeftRegion(ActiveRegion* reg) {
		Vertex* org = reg->eUp->org;

		// Region above the uppermost edge with the same origin.
		do {
			reg = regionAbove(reg);
		} while (reg->eUp->org == org);

		// If the edge above was a temporary edge from connectRightVertex, fix it now.
		if (reg->fixUpperEdge) {
			HalfEdge* e = mesh.connect(regionBelow(reg)->eUp->sym, reg->eUp->lnext);
			replaceUpperEdge(reg, e);
			reg = regionAbove(reg);
		}
		return reg;
	}

	ActiveRegion* Sweep::topRightRegion(ActiveRegion* reg) {
		Vertex* dst = reg->eUp->dst();
		do {
			reg = regionAbove(reg);
		} while (reg->eUp->dst() == dst);
		return reg;
	}

	ActiveRegion* Sweep::addRegionBelow(ActiveRegion* regAbove, HalfEdge* eNewUp) {
		ActiveRegion* regNew = allocRegion();
		regNew->eUp = eNewUp;
		regNew->nodeUp = dict->insertBefore(regAbove->nodeUp, regNew);
		regNew->fixUpperEdge = false;
		regNew->sentinel = false;
		regNew->dirty = false;
		eNewUp->activeRegion = regNew;
		return regNew;
	}

	bool Sweep::isWindingInside(int n) const {
		switch (windingRule) {
			case WindingRule::Odd:
				return (n & 1) != 0;
			case WindingRule::NonZero:
				return n != 0;
		}
		return false;
	}

	void Sweep::computeWinding(ActiveRegion* reg) {
		reg->windingNumber = regionAbove(reg)->windingNumber + reg->eUp->winding;
		reg->inside = isWindingInside(reg->windingNumber);
	}

	void Sweep::finishRegion(ActiveRegion* reg) {
		// The upper/lower chains have met; copy the inside flag to the mesh face.
		HalfEdge* e = reg->eUp;
		Face*	  f = e->lface;
		f->inside = reg->inside;
		f->anEdge = e; // optimization for the monotone triangulation
		deleteRegion(reg);
	}

	HalfEdge* Sweep::finishLeftRegions(ActiveRegion* regFirst, ActiveRegion* regLast) {
		ActiveRegion* regPrev = regFirst;
		HalfEdge*	  ePrev = regFirst->eUp;
		while (regPrev != regLast) {
			regPrev->fixUpperEdge = false; // placement was OK
			ActiveRegion* reg = regionBelow(regPrev);
			HalfEdge*	  e = reg->eUp;
			if (e->org != ePrev->org) {
				if (!reg->fixUpperEdge) {
					finishRegion(regPrev);
					break;
				}
				// Fix a temporary edge from connectRightVertex.
				e = mesh.connect(ePrev->lprev(), e->sym);
				replaceUpperEdge(reg, e);
			}
			// Relink so ePrev->onext == e.
			if (ePrev->onext != e) {
				mesh.splice(e->oprev(), e);
				mesh.splice(ePrev, e);
			}
			finishRegion(regPrev); // may change reg->eUp
			ePrev = reg->eUp;
			regPrev = reg;
		}
		return ePrev;
	}

	void Sweep::addRightEdges(ActiveRegion* regUp, HalfEdge* eFirst, HalfEdge* eLast,
							  HalfEdge* eTopLeft, bool cleanUp) {
		bool firstTime = true;

		// Insert the new right-going edges into the dictionary.
		HalfEdge* e = eFirst;
		do {
			assert(vertLeq(*e->org, *e->dst()));
			addRegionBelow(regUp, e->sym);
			e = e->onext;
		} while (e != eLast);

		if (eTopLeft == nullptr) {
			eTopLeft = regionBelow(regUp)->eUp->rprev();
		}
		ActiveRegion* regPrev = regUp;
		HalfEdge*	  ePrev = eTopLeft;
		ActiveRegion* reg = nullptr;
		for (;;) {
			reg = regionBelow(regPrev);
			e = reg->eUp->sym;
			if (e->org != ePrev->org) {
				break;
			}
			if (e->onext != ePrev) {
				// Unlink e and relink below ePrev.
				mesh.splice(e->oprev(), e);
				mesh.splice(ePrev->oprev(), e);
			}
			reg->windingNumber = regPrev->windingNumber - e->winding;
			reg->inside = isWindingInside(reg->windingNumber);

			regPrev->dirty = true;
			if (!firstTime && checkForRightSplice(regPrev)) {
				addWinding(e, ePrev);
				deleteRegion(regPrev);
				mesh.deleteEdge(ePrev);
			}
			firstTime = false;
			regPrev = reg;
			ePrev = e;
		}
		regPrev->dirty = true;
		assert(regPrev->windingNumber - e->winding == reg->windingNumber);

		if (cleanUp) {
			walkDirtyRegions(regPrev);
		}
	}

	void Sweep::spliceMergeVertices(HalfEdge* e1, HalfEdge* e2) { mesh.splice(e1, e2); }

	bool Sweep::checkForRightSplice(ActiveRegion* regUp) {
		ActiveRegion* regLo = regionBelow(regUp);
		HalfEdge*	  eUp = regUp->eUp;
		HalfEdge*	  eLo = regLo->eUp;

		if (vertLeq(*eUp->org, *eLo->org)) {
			if (edgeSign(*eLo->dst(), *eUp->org, *eLo->org) > 0.0F) {
				return false;
			}
			if (!vertEq(*eUp->org, *eLo->org)) {
				// Splice eUp->org into eLo.
				mesh.splitEdge(eLo->sym);
				mesh.splice(eUp, eLo->oprev());
				regUp->dirty = regLo->dirty = true;
			} else if (eUp->org != eLo->org) {
				// Merge the two vertices, discarding eUp->org.
				pq.remove(eUp->org->pqHandle);
				spliceMergeVertices(eLo->oprev(), eUp);
			}
		} else {
			if (edgeSign(*eUp->dst(), *eLo->org, *eUp->org) < 0.0F) {
				return false;
			}
			// Splice eLo->org into eUp.
			regUp->dirty = true;
			ActiveRegion* above = regionAbove(regUp);
			if (above != nullptr) {
				above->dirty = true;
			}
			mesh.splitEdge(eUp->sym);
			mesh.splice(eLo->oprev(), eUp);
		}
		return true;
	}

	bool Sweep::checkForLeftSplice(ActiveRegion* regUp) {
		ActiveRegion* regLo = regionBelow(regUp);
		HalfEdge*	  eUp = regUp->eUp;
		HalfEdge*	  eLo = regLo->eUp;

		assert(!vertEq(*eUp->dst(), *eLo->dst()));

		if (vertLeq(*eUp->dst(), *eLo->dst())) {
			if (edgeSign(*eUp->dst(), *eLo->dst(), *eUp->org) < 0.0F) {
				return false;
			}
			// Splice eLo->dst into eUp.
			regUp->dirty = true;
			ActiveRegion* above = regionAbove(regUp);
			if (above != nullptr) {
				above->dirty = true;
			}
			HalfEdge* e = mesh.splitEdge(eUp);
			mesh.splice(eLo->sym, e);
			e->lface->inside = regUp->inside;
		} else {
			if (edgeSign(*eLo->dst(), *eUp->dst(), *eLo->org) > 0.0F) {
				return false;
			}
			// Splice eUp->dst into eLo.
			regUp->dirty = regLo->dirty = true;
			HalfEdge* e = mesh.splitEdge(eLo);
			mesh.splice(eUp->lnext, eLo->sym);
			e->rface()->inside = regUp->inside;
		}
		return true;
	}

	bool Sweep::checkForIntersect(ActiveRegion* regUp) {
		ActiveRegion* regLo = regionBelow(regUp);
		HalfEdge*	  eUp = regUp->eUp;
		HalfEdge*	  eLo = regLo->eUp;
		Vertex*		  orgUp = eUp->org;
		Vertex*		  orgLo = eLo->org;
		Vertex*		  dstUp = eUp->dst();
		Vertex*		  dstLo = eLo->dst();
		Vertex		  isect{};

		assert(!vertEq(*dstLo, *dstUp));
		assert(edgeSign(*dstUp, *event, *orgUp) <= 0.0F);
		assert(edgeSign(*dstLo, *event, *orgLo) >= 0.0F);
		assert(orgUp != event && orgLo != event);
		assert(!regUp->fixUpperEdge && !regLo->fixUpperEdge);

		if (orgUp == orgLo) {
			return false; // right endpoints are the same
		}

		const float tMinUp = std::min(orgUp->t, dstUp->t);
		const float tMaxLo = std::max(orgLo->t, dstLo->t);
		if (tMinUp > tMaxLo) {
			return false; // t ranges do not overlap
		}

		if (vertLeq(*orgUp, *orgLo)) {
			if (edgeSign(*dstLo, *orgUp, *orgLo) > 0.0F) {
				return false;
			}
		} else {
			if (edgeSign(*dstUp, *orgLo, *orgUp) < 0.0F) {
				return false;
			}
		}

		edgeIntersect(*dstUp, *orgUp, *dstLo, *orgLo, isect);
		assert(std::min(orgUp->t, dstUp->t) <= isect.t + FLT_MIN);
		assert(isect.t <= std::max(orgLo->t, dstLo->t) + FLT_MIN);
		assert(std::min(dstLo->s, dstUp->s) <= isect.s + FLT_MIN);
		assert(isect.s <= std::max(orgLo->s, orgUp->s) + FLT_MIN);

		if (vertLeq(isect, *event)) {
			// Move a slightly-left intersection onto the sweep event.
			isect.s = event->s;
			isect.t = event->t;
		}
		Vertex* orgMin = vertLeq(*orgUp, *orgLo) ? orgUp : orgLo;
		if (vertLeq(*orgMin, isect)) {
			isect.s = orgMin->s;
			isect.t = orgMin->t;
		}

		if (vertEq(isect, *orgUp) || vertEq(isect, *orgLo)) {
			// Intersection at one of the right endpoints.
			checkForRightSplice(regUp);
			return false;
		}

		if ((!vertEq(*dstUp, *event) && edgeSign(*dstUp, *event, isect) >= 0.0F)
			|| (!vertEq(*dstLo, *event) && edgeSign(*dstLo, *event, isect) <= 0.0F)) {
			// The new edge would pass on the wrong side of the event (tiny numeric error).
			if (dstLo == event) {
				mesh.splitEdge(eUp->sym);
				mesh.splice(eLo->sym, eUp);
				regUp = topLeftRegion(regUp);
				eUp = regionBelow(regUp)->eUp;
				finishLeftRegions(regionBelow(regUp), regLo);
				addRightEdges(regUp, eUp->oprev(), eUp, eUp, true);
				return true;
			}
			if (dstUp == event) {
				mesh.splitEdge(eLo->sym);
				mesh.splice(eUp->lnext, eLo->oprev());
				regLo = regUp;
				regUp = topRightRegion(regUp);
				HalfEdge* e = regionBelow(regUp)->eUp->rprev();
				regLo->eUp = eLo->oprev();
				eLo = finishLeftRegions(regLo, nullptr);
				addRightEdges(regUp, eLo->onext, eUp->rprev(), e, true);
				return true;
			}
			// Split whichever edge passes on the wrong side; leave the rest for
			// connectRightVertex.
			if (edgeSign(*dstUp, *event, isect) >= 0.0F) {
				regionAbove(regUp)->dirty = regUp->dirty = true;
				mesh.splitEdge(eUp->sym);
				eUp->org->s = event->s;
				eUp->org->t = event->t;
			}
			if (edgeSign(*dstLo, *event, isect) <= 0.0F) {
				regUp->dirty = regLo->dirty = true;
				mesh.splitEdge(eLo->sym);
				eLo->org->s = event->s;
				eLo->org->t = event->t;
			}
			return false;
		}

		// General case: split both edges and splice into the new vertex.
		mesh.splitEdge(eUp->sym);
		mesh.splitEdge(eLo->sym);
		mesh.splice(eLo->oprev(), eUp);
		eUp->org->s = isect.s;
		eUp->org->t = isect.t;
		eUp->org->idx = -1;
		eUp->org->pqHandle = pq.insert(eUp->org);
		regionAbove(regUp)->dirty = regUp->dirty = regLo->dirty = true;
		return false;
	}

	void Sweep::walkDirtyRegions(ActiveRegion* regUp) {
		ActiveRegion* regLo = regionBelow(regUp);

		for (;;) {
			// Walk from the bottom up to the lowest dirty region.
			while (regLo->dirty) {
				regUp = regLo;
				regLo = regionBelow(regLo);
			}
			if (!regUp->dirty) {
				regLo = regUp;
				regUp = regionAbove(regUp);
				if (regUp == nullptr || !regUp->dirty) {
					return;
				}
			}
			regUp->dirty = false;
			HalfEdge* eUp = regUp->eUp;
			HalfEdge* eLo = regLo->eUp;

			if (eUp->dst() != eLo->dst()) {
				// Check edge ordering at the dst vertices.
				if (checkForLeftSplice(regUp)) {
					if (regLo->fixUpperEdge) {
						deleteRegion(regLo);
						mesh.deleteEdge(eLo);
						regLo = regionBelow(regUp);
						eLo = regLo->eUp;
					} else if (regUp->fixUpperEdge) {
						deleteRegion(regUp);
						mesh.deleteEdge(eUp);
						regUp = regionAbove(regLo);
						eUp = regUp->eUp;
					}
				}
			}
			if (eUp->org != eLo->org) {
				if (eUp->dst() != eLo->dst() && !regUp->fixUpperEdge && !regLo->fixUpperEdge
					&& (eUp->dst() == event || eLo->dst() == event)) {
					if (checkForIntersect(regUp)) {
						// walkDirtyRegions recursed; we're done.
						return;
					}
				} else {
					checkForRightSplice(regUp);
				}
			}
			if (eUp->org == eLo->org && eUp->dst() == eLo->dst()) {
				// Degenerate two-edge loop -- delete it.
				addWinding(eLo, eUp);
				deleteRegion(regUp);
				mesh.deleteEdge(eUp);
				regUp = regionAbove(regLo);
			}
		}
	}

	void Sweep::connectRightVertex(ActiveRegion* regUp, HalfEdge* eBottomLeft) {
		HalfEdge*	  eTopLeft = eBottomLeft->onext;
		ActiveRegion* regLo = regionBelow(regUp);
		HalfEdge*	  eUp = regUp->eUp;
		HalfEdge*	  eLo = regLo->eUp;
		bool		  degenerate = false;

		if (eUp->dst() != eLo->dst()) {
			checkForIntersect(regUp);
		}

		// The upper or lower edge of regUp may now pass through the event.
		if (vertEq(*eUp->org, *event)) {
			mesh.splice(eTopLeft->oprev(), eUp);
			regUp = topLeftRegion(regUp);
			eTopLeft = regionBelow(regUp)->eUp;
			finishLeftRegions(regionBelow(regUp), regLo);
			degenerate = true;
		}
		if (vertEq(*eLo->org, *event)) {
			mesh.splice(eBottomLeft, eLo->oprev());
			eBottomLeft = finishLeftRegions(regLo, nullptr);
			degenerate = true;
		}
		if (degenerate) {
			addRightEdges(regUp, eBottomLeft->onext, eTopLeft, eTopLeft, true);
			return;
		}

		// Add a temporary, fixable edge to the closer of eLo->org, eUp->org.
		HalfEdge* eNew = vertLeq(*eLo->org, *eUp->org) ? eLo->oprev() : eUp;
		eNew = mesh.connect(eBottomLeft->lprev(), eNew);

		// cleanUp == false so eNew survives long enough to be marked fixable.
		addRightEdges(regUp, eNew, eNew->onext, eNew->onext, false);
		eNew->sym->activeRegion->fixUpperEdge = true;
		walkDirtyRegions(regUp);
	}

	void Sweep::connectLeftDegenerate(ActiveRegion* regUp, Vertex* vEvent) {
		HalfEdge* e = regUp->eUp;
		if (vertEq(*e->org, *vEvent)) {
			// e->org is unprocessed; combine and wait for it to be dequeued.
			spliceMergeVertices(e, vEvent->anEdge);
			return;
		}

		if (!vertEq(*e->dst(), *vEvent)) {
			// General case -- splice vEvent into edge e which passes through it.
			mesh.splitEdge(e->sym);
			if (regUp->fixUpperEdge) {
				mesh.deleteEdge(e->onext);
				regUp->fixUpperEdge = false;
			}
			mesh.splice(vEvent->anEdge, e);
			sweepEvent(vEvent); // recurse
			return;
		}

		// vEvent coincides with e->dst (already processed); splice in right-going edges.
		regUp = topRightRegion(regUp);
		ActiveRegion* reg = regionBelow(regUp);
		HalfEdge*	  eTopRight = reg->eUp->sym;
		HalfEdge*	  eTopLeft = eTopRight->onext;
		HalfEdge*	  eLast = eTopLeft;
		if (reg->fixUpperEdge) {
			deleteRegion(reg);
			mesh.deleteEdge(eTopRight);
			eTopRight = eTopLeft->oprev();
		}
		mesh.splice(vEvent->anEdge, eTopRight);
		if (!edgeGoesLeft(*eTopLeft)) {
			eTopLeft = nullptr;
		}
		addRightEdges(regUp, eTopRight->onext, eLast, eTopLeft, true);
	}

	void Sweep::connectLeftVertex(Vertex* vEvent) {
		ActiveRegion tmp;
		tmp.eUp = vEvent->anEdge->sym;
		ActiveRegion* regUp = dictKey(dict->search(&tmp));
		ActiveRegion* regLo = regionBelow(regUp);
		if (regLo == nullptr) {
			return; // can happen if the input is coplanar/degenerate
		}
		HalfEdge* eUp = regUp->eUp;
		HalfEdge* eLo = regLo->eUp;

		// Try merging with the upper or lower chain first.
		if (edgeSign(*eUp->dst(), *vEvent, *eUp->org) == 0.0F) {
			connectLeftDegenerate(regUp, vEvent);
			return;
		}

		// Connect vEvent to the rightmost processed vertex of either chain.
		ActiveRegion* reg = vertLeq(*eLo->dst(), *eUp->dst()) ? regUp : regLo;
		if (regUp->inside || reg->fixUpperEdge) {
			HalfEdge* eNew = nullptr;
			if (reg == regUp) {
				eNew = mesh.connect(vEvent->anEdge->sym, eUp->lnext);
			} else {
				HalfEdge* tempHalfEdge = mesh.connect(eLo->dnext(), vEvent->anEdge);
				eNew = tempHalfEdge->sym;
			}
			if (reg->fixUpperEdge) {
				replaceUpperEdge(reg, eNew);
			} else {
				computeWinding(addRegionBelow(regUp, eNew));
			}
			sweepEvent(vEvent);
		} else {
			// The new vertex is in an exterior region; no need to connect it.
			addRightEdges(regUp, vEvent->anEdge, vEvent->anEdge, nullptr, true);
		}
	}

	void Sweep::sweepEvent(Vertex* vEvent) {
		event = vEvent; // for edgeLeq()

		// If vEvent is the right endpoint of an already-inserted edge, skip the search.
		HalfEdge* e = vEvent->anEdge;
		while (e->activeRegion == nullptr) {
			e = e->onext;
			if (e == vEvent->anEdge) {
				connectLeftVertex(vEvent); // all edges go right
				return;
			}
		}

		// Finish all regions that vEvent closes off (the left-going edges).
		ActiveRegion* regUp = topLeftRegion(e->activeRegion);
		ActiveRegion* reg = regionBelow(regUp);
		HalfEdge*	  eTopLeft = reg->eUp;
		HalfEdge*	  eBottomLeft = finishLeftRegions(reg, nullptr);

		// Process the right-going edges from vEvent.
		if (eBottomLeft->onext == eTopLeft) {
			connectRightVertex(regUp, eBottomLeft); // no right-going edges
		} else {
			addRightEdges(regUp, eBottomLeft->onext, eTopLeft, eTopLeft, true);
		}
	}

	void Sweep::addSentinel(float smin, float smax, float t) {
		ActiveRegion* reg = allocRegion();
		HalfEdge*	  e = mesh.makeEdge();

		e->org->s = smax;
		e->org->t = t;
		e->dst()->s = smin;
		e->dst()->t = t;
		event = e->dst(); // initialize

		reg->eUp = e;
		reg->windingNumber = 0;
		reg->inside = false;
		reg->fixUpperEdge = false;
		reg->sentinel = true;
		reg->dirty = false;
		reg->nodeUp = dict->insert(reg);
	}

	void Sweep::initEdgeDict() {
		dict = std::make_unique<Dict>(this, &Sweep::edgeLeqThunk);

		// Enlarge an empty bbox slightly so the sentinels aren't coincident.
		const float w = (bmax[0] - bmin[0]) + 0.01F;
		const float h = (bmax[1] - bmin[1]) + 0.01F;
		const float smin = bmin[0] - w;
		const float smax = bmax[0] + w;
		const float tmin = bmin[1] - h;
		const float tmax = bmax[1] + h;

		addSentinel(smin, smax, tmin);
		addSentinel(smin, smax, tmax);
	}

	void Sweep::doneEdgeDict() {
		ActiveRegion* reg = nullptr;
		while ((reg = dictKey(dict->min())) != nullptr) {
			deleteRegion(reg);
		}
		dict.reset();
	}

	void Sweep::removeDegenerateEdges() {
		HalfEdge* eHead = &mesh.eHead;
		HalfEdge* eNext = nullptr;
		for (HalfEdge* e = eHead->next; e != eHead; e = eNext) {
			eNext = e->next;
			HalfEdge* eLnext = e->lnext;

			if (vertEq(*e->org, *e->dst()) && e->lnext->lnext != e) {
				// Zero-length edge, contour has at least 3 edges.
				spliceMergeVertices(eLnext, e); // deletes e->org
				mesh.deleteEdge(e);				// e is a self-loop
				e = eLnext;
				eLnext = e->lnext;
			}
			if (eLnext->lnext == e) {
				// Degenerate contour (one or two edges).
				if (eLnext != e) {
					if (eLnext == eNext || eLnext == eNext->sym) {
						eNext = eNext->next;
					}
					mesh.deleteEdge(eLnext);
				}
				if (e == eNext || e == eNext->sym) {
					eNext = eNext->next;
				}
				mesh.deleteEdge(e);
			}
		}
	}

	void Sweep::initPriorityQ() {
		Vertex* vHead = &mesh.vHead;
		for (Vertex* v = vHead->next; v != vHead; v = v->next) {
			v->pqHandle = pq.insert(v);
		}
	}

	void Sweep::removeDegenerateFaces() {
		Face* fNext = nullptr;
		for (Face* f = mesh.fHead.next; f != &mesh.fHead; f = fNext) {
			fNext = f->next;
			HalfEdge* e = f->anEdge;
			assert(e->lnext != e);
			if (e->lnext->lnext == e) {
				// A face with only two edges.
				addWinding(e->onext, e);
				mesh.deleteEdge(e);
			}
		}
	}

	void Sweep::computeInterior() {
		removeDegenerateEdges();
		initPriorityQ();
		initEdgeDict();

		Vertex* v = nullptr;
		while ((v = pq.extractMin()) != nullptr) {
			for (;;) {
				Vertex* vNext = pq.minimum();
				if (vNext == nullptr || !vertEq(*vNext, *v)) {
					break;
				}
				// Merge all vertices at exactly the same location.
				vNext = pq.extractMin();
				spliceMergeVertices(v->anEdge, vNext->anEdge);
			}
			sweepEvent(v);
		}

		doneEdgeDict();
		removeDegenerateFaces();
	}

} // namespace renderer::tess
