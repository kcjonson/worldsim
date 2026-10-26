// hash_noise.glsl - foundation::hash3, gradientNoise2PairCell and
// fractalNoise2SplitPair (libs/foundation/random/HashNoise.h) in GLSL. The hash is
// integer math and matches the C++ bit for bit; the float steps are the same
// expressions, so a value agrees with the C++ to within rounding (a GPU compiler may
// fuse a multiply-add). SurfaceFieldGolden.test.cpp holds the two together through
// the land field.

uint noiseHash3(int x, int y, int z, uint seed) {
	uint h = 0x811C9DC5u;
	h ^= seed;
	h *= 0xBF58476Du;
	h ^= h >> 16u;
	h ^= uint(x);
	h *= 0x94D049BBu;
	h ^= h >> 16u;
	h ^= uint(y);
	h *= 0xBF58476Du;
	h ^= h >> 16u;
	h ^= uint(z);
	h *= 0x94D049BBu;
	h ^= h >> 16u;
	h ^= h >> 15u;
	h *= 0x85EBCA77u;
	h ^= h >> 13u;
	h *= 0xC2B2AE3Du;
	h ^= h >> 16u;
	return h;
}

// The x and y of detail::gradient's 16 entries (its z only ever multiplies zero in
// 2D), each stored as g + 1 in two bits of a constant. A const array indexed by a
// hash would be copied to local memory in every fragment.
const uint kNoiseGradientX = 0x52552222u;
const uint kNoiseGradientY = 0x0A22550Au;

float noiseQuintic(float t) {
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

/// detail::gradient's entry `index` (0..15), x and y.
vec2 noiseGradient(uint index) {
	return vec2(ivec2(uvec2(kNoiseGradientX, kNoiseGradientY) >> (index * 2u) & 3u)) - 1.0;
}

/// The two fields of gradientNoise2PairCell at one corner: gradients from bits 0-3
/// and 4-7 of the corner's hash.
vec2 noiseCornerPair(ivec2 cell, vec2 d, uint seed) {
	uint h	= noiseHash3(cell.x, cell.y, 0, seed);
	vec2 g1 = noiseGradient(h & 15u);
	vec2 g2 = noiseGradient((h >> 4u) & 15u);
	return vec2(g1.x * d.x + g1.y * d.y, g2.x * d.x + g2.y * d.y);
}

vec2 gradientNoise2PairCell(ivec2 cell, vec2 f, uint seed) {
	float ux  = noiseQuintic(f.x);
	float uy  = noiseQuintic(f.y);
	vec2  g00 = noiseCornerPair(cell, f, seed);
	vec2  g10 = noiseCornerPair(cell + ivec2(1, 0), vec2(f.x - 1.0, f.y), seed);
	vec2  g01 = noiseCornerPair(cell + ivec2(0, 1), vec2(f.x, f.y - 1.0), seed);
	vec2  g11 = noiseCornerPair(cell + ivec2(1, 1), vec2(f.x - 1.0, f.y - 1.0), seed);
	vec2  x0  = g00 + ux * (g10 - g00);
	vec2  x1  = g01 + ux * (g11 - g01);
	return x0 + uy * (x1 - x0);
}

int noiseFloorDiv(int a, int b) {
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// fractalNoise2SplitPair with lacunarity 2, summing only the first `summed` of its
// `octaves` (a far-zoom LOD drops the finer ones) but normalizing over all of them,
// so each kept octave weighs what it does in the full sum. `invWavelength` must be
// the CPU's 1.0F / float(wavelength), passed in so both sides scale by the same float.
vec2 fractalNoise2SplitPair(
	ivec2 whole, vec2 frac, int wavelength, float invWavelength, uint seed, int octaves, float gain, int summed
) {
	ivec2 q		= ivec2(noiseFloorDiv(whole.x, wavelength), noiseFloorDiv(whole.y, wavelength));
	vec2  base	= vec2(whole - q * wavelength) + frac;
	vec2  value = vec2(0.0);
	float amp	= 1.0;
	float norm	= 0.0;
	for (int i = 0; i < octaves; ++i) {
		if (i < summed) {
			int	  cells = 1 << i;
			vec2  u		= base * (invWavelength * float(cells));
			ivec2 c		= ivec2(u); // u >= 0: truncation is floor
			value += gradientNoise2PairCell(q * cells + c, u - vec2(c), seed + uint(i)) * amp;
		}
		norm += amp;
		amp *= gain;
	}
	return norm > 0.0 ? value / norm : vec2(0.0);
}
