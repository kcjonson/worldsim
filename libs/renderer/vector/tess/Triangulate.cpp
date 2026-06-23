#include "Triangulate.h"

#include "Predicates.h"

#include <cassert>

namespace renderer::tess {

	namespace {
		// Triangulate a single monotone region (CCW loop) by walking its upper and lower
		// chains right to left, fanning triangles off whichever chain endpoint is leftmost.
		void tessellateMonoRegion(Mesh& mesh, Face* face) {
			// Find the rightmost origin vertex; face->anEdge is near it after the sweep.
			HalfEdge* up = face->anEdge;
			assert(up->lnext != up && up->lnext->lnext != up);

			for (; vertLeq(*up->dst(), *up->org); up = up->lprev()) {
			}
			for (; vertLeq(*up->org, *up->dst()); up = up->lnext) {
			}
			HalfEdge* lo = up->lprev();

			while (up->lnext != lo) {
				if (vertLeq(*up->dst(), *lo->org)) {
					// up->dst is leftmost: fan triangles off lo->org.
					while (lo->lnext != up
						   && (edgeGoesLeft(*lo->lnext)
							   || edgeSign(*lo->org, *lo->dst(), *lo->lnext->dst()) <= 0.0F)) {
						HalfEdge* temp = mesh.connect(lo->lnext, lo);
						lo = temp->sym;
					}
					lo = lo->lprev();
				} else {
					// lo->org is leftmost: fan CCW triangles off up->dst.
					while (lo->lnext != up
						   && (edgeGoesRight(*up->lprev())
							   || edgeSign(*up->dst(), *up->org, *up->lprev()->org) >= 0.0F)) {
						HalfEdge* temp = mesh.connect(up, up->lprev());
						up = temp->sym;
					}
					up = up->lnext;
				}
			}

			// The remaining region is a fan from the leftmost vertex.
			assert(lo->lnext != up);
			while (lo->lnext->lnext != up) {
				HalfEdge* temp = mesh.connect(lo->lnext, lo);
				lo = temp->sym;
			}
		}
	} // namespace

	void tessellateInterior(Mesh& mesh) {
		Face* next = nullptr;
		for (Face* f = mesh.fHead.next; f != &mesh.fHead; f = next) {
			next = f->next; // the new triangles must not be re-tessellated
			if (f->inside) {
				tessellateMonoRegion(mesh, f);
			}
		}
	}

} // namespace renderer::tess
