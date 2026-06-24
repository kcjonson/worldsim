#include "Predicates.h"

#include <utility>

namespace renderer::tess {

	namespace {
		// Returns (b*x + a*y)/(a+b), or (x+y)/2 when a==b==0. Requires a,b >= 0 (negative
		// inputs are clamped). Numerically stable: the result r satisfies min(x,y) <= r <= max(x,y).
		float interpolate(float a, float x, float b, float y) {
			a = (a < 0.0F) ? 0.0F : a;
			b = (b < 0.0F) ? 0.0F : b;
			if (a <= b) {
				return (b == 0.0F) ? ((x / 2.0F) + (y / 2.0F)) : (x + ((y - x) * (a / (a + b))));
			}
			return y + ((x - y) * (b / (a + b)));
		}
	} // namespace

	float edgeEval(const Vertex& u, const Vertex& v, const Vertex& w) {
		// Evaluate the t of edge uw at v.s, returning v.t minus that (signed distance).
		const float gapL = v.s - u.s;
		const float gapR = w.s - v.s;
		if (gapL + gapR > 0.0F) {
			if (gapL < gapR) {
				return (v.t - u.t) + ((u.t - w.t) * (gapL / (gapL + gapR)));
			}
			return (v.t - w.t) + ((w.t - u.t) * (gapR / (gapL + gapR)));
		}
		return 0.0F; // vertical line
	}

	float transEval(const Vertex& u, const Vertex& v, const Vertex& w) {
		const float gapL = v.t - u.t;
		const float gapR = w.t - v.t;
		if (gapL + gapR > 0.0F) {
			if (gapL < gapR) {
				return (v.s - u.s) + ((u.s - w.s) * (gapL / (gapL + gapR)));
			}
			return (v.s - w.s) + ((w.s - u.s) * (gapR / (gapL + gapR)));
		}
		return 0.0F; // horizontal line
	}

	float transSign(const Vertex& u, const Vertex& v, const Vertex& w) {
		const float gapL = v.t - u.t;
		const float gapR = w.t - v.t;
		if (gapL + gapR > 0.0F) {
			return ((v.s - w.s) * gapL) + ((v.s - u.s) * gapR);
		}
		return 0.0F; // horizontal line
	}

	bool vertCcw(const Vertex& u, const Vertex& v, const Vertex& w) {
		return ((u.s * (v.t - w.t)) + (v.s * (w.t - u.t)) + (w.s * (u.t - v.t))) >= 0.0F;
	}

	void edgeIntersect(const Vertex& io1, const Vertex& id1, const Vertex& io2, const Vertex& id2, Vertex& out) {
		// Find the two middle vertices in the order and interpolate the intersection
		// coordinate from them; repeat with s/t transposed for the other coordinate.
		const Vertex* o1 = &io1;
		const Vertex* d1 = &id1;
		const Vertex* o2 = &io2;
		const Vertex* d2 = &id2;

		// --- s coordinate ---
		if (!vertLeq(*o1, *d1)) {
			std::swap(o1, d1);
		}
		if (!vertLeq(*o2, *d2)) {
			std::swap(o2, d2);
		}
		if (!vertLeq(*o1, *o2)) {
			std::swap(o1, o2);
			std::swap(d1, d2);
		}

		if (!vertLeq(*o2, *d1)) {
			out.s = (o2->s / 2.0F) + (d1->s / 2.0F); // no real intersection; best effort
		} else if (vertLeq(*d1, *d2)) {
			float z1 = edgeEval(*o1, *o2, *d1);
			float z2 = edgeEval(*o2, *d1, *d2);
			if (z1 + z2 < 0.0F) {
				z1 = -z1;
				z2 = -z2;
			}
			out.s = interpolate(z1, o2->s, z2, d1->s);
		} else {
			float z1 = edgeSign(*o1, *o2, *d1);
			float z2 = -edgeSign(*o1, *d2, *d1);
			if (z1 + z2 < 0.0F) {
				z1 = -z1;
				z2 = -z2;
			}
			out.s = interpolate(z1, o2->s, z2, d2->s);
		}

		// --- t coordinate ---
		o1 = &io1;
		d1 = &id1;
		o2 = &io2;
		d2 = &id2;
		if (!transLeq(*o1, *d1)) {
			std::swap(o1, d1);
		}
		if (!transLeq(*o2, *d2)) {
			std::swap(o2, d2);
		}
		if (!transLeq(*o1, *o2)) {
			std::swap(o1, o2);
			std::swap(d1, d2);
		}

		if (!transLeq(*o2, *d1)) {
			out.t = (o2->t / 2.0F) + (d1->t / 2.0F); // no real intersection; best effort
		} else if (transLeq(*d1, *d2)) {
			float z1 = transEval(*o1, *o2, *d1);
			float z2 = transEval(*o2, *d1, *d2);
			if (z1 + z2 < 0.0F) {
				z1 = -z1;
				z2 = -z2;
			}
			out.t = interpolate(z1, o2->t, z2, d1->t);
		} else {
			float z1 = transSign(*o1, *o2, *d1);
			float z2 = -transSign(*o1, *d2, *d1);
			if (z1 + z2 < 0.0F) {
				z1 = -z1;
				z2 = -z2;
			}
			out.t = interpolate(z1, o2->t, z2, d2->t);
		}
	}

} // namespace renderer::tess
