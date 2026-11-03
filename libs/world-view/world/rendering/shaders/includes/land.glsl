// land.glsl - the land pass: every land-on-land boundary is the isoline of a
// softened, domain-warped per-surface field
// (docs/technical/organic-terrain/terrain-polygons-architecture.md D16), never a
// tile edge. evaluateLand() with every fine warp octave is
// world::evaluateSurfaceField (SurfaceField.cpp) step for step, in the same
// order and the same expressions; SurfaceFieldGolden.test.cpp renders it and
// compares the two. The picture sums only the octaves a pixel can show (D16 step 7).
// Each surface paints with its baked ground tiles, hex-tiled and tinted by the
// chunk's macro texture (docs/technical/ground-textures.md, "Shader").
//
// Positions are a whole tile plus the offset inside it: the noise and every lattice
// read are formed from those two parts, never from a large float world coordinate.

#include "hash_noise.glsl"

// ============================================================================
// TEXTURES AND UNIFORMS
// ============================================================================

// Render tiles (RenderTiles.h): RG8UI over the chunk square plus 3 tiles each side.
// R: paint surface id. G: edge surface id | interior bit.
uniform usampler2D u_tileData;
uniform ivec2	   u_chunkTileOrigin; // world tile of the chunk's tile (0, 0)

// Ground tiles (GroundTileBank): sRGB texture arrays, one per layer size, every
// mip baked from the vectors, alpha the baked height. Per surface id:
//   u_groundLayers: x array (0..3), y first layer, z layer count (at least 1), w rotate (low 2 bits:
//   0 free, 1 quarter, 2 none, 3 mirror) | the surface id its cells hash << 2 (its seed group's lowest)
//   u_groundParams: x 1 / period (m), y mip level this draw, z 1 / layer count, w 0.5 z (groundVariant)
//   u_groundTintBase, u_groundTintLush: the macro tint, base + lush * the macro's R,
//   multiplied into the sRGB color
uniform sampler2DArray u_groundTiles0;
uniform sampler2DArray u_groundTiles1;
uniform sampler2DArray u_groundTiles2;
uniform sampler2DArray u_groundTiles3;
uniform ivec4		   u_groundLayers[10];
uniform vec4		   u_groundParams[10];
uniform vec3		   u_groundTintBase[10];
uniform vec3		   u_groundTintLush[10];
uniform uint		   u_groundSeed;
uniform ivec2		   u_groundCellOrigin;	   // the chunk's first hex cell id (see THE LOOK)
uniform bool		   u_groundHex;			   // px/m at or over terrain/ground/hexMinPxPerMeter
uniform float		   u_groundBlendContrast;  // exponent on the barycentric weights
uniform float		   u_groundBlendGainK;	   // log(1 - r) / log(0.5) for the zoom's gain r; 1 is none
uniform vec3		   u_groundMacroValue;	   // value scale: x + y * macro G + z * macro B

// The chunk's macro texture (GroundMacro.h): 257x257, a texel every 2 m from the
// chunk's corner, its last row and column the next chunk's first. R the dry to lush
// mix, G and B value noise around 0.5.
uniform sampler2D u_groundMacro;

// The field (SurfaceFieldParams): per surface id where indexed.
uniform float u_landWarpFine[10];
uniform float u_landWarpLow[10];
uniform ivec2 u_landWarpWavelength;	   // fine, low; whole meters
uniform vec2  u_landWarpInvWavelength; // 1 / wavelength, as the CPU computes it
uniform float u_landWarpFineGain;
uniform uvec2 u_landWarpSeeds;		   // fine, low
uniform float u_landThinFloor;
uniform float u_landThinCeil;
// Fine warp octaves drawn at this zoom (landWarpFineOctaves, D16 step 7): the finer
// ones would move an edge by a fraction of a pixel.
uniform int u_landWarpFineOctaves;

// The edge look (ground-textures.md, Surface transitions): two vec4 for each pair of
// D13 levels, upper above lower, at pair i = upper * (upper - 1) / 2 + lower
// (landPairIndex), from the upper surface's def with this draw's mip window folded
// in (GroundPairs.h packGroundPairs):
//   [2 i]     x 0.5 / w, y 0.5 + 0.5 o / w, z 1 / (2 W), w nu / (4 W)
//   [2 i + 1] shadow, stain, ridge, tint
uniform vec4 u_landPair[72];

// ============================================================================
// CONSTANTS (RenderTiles.h, SurfaceField.h)
// ============================================================================

const int	kRenderApronTiles	   = 3;
const float kRenderTilesInvSide	   = 1.0 / 518.0; // kRenderTilesSide
const uint	kRenderEdgeSurfaceMask = 0x0Fu;
const uint	kRenderNearInteriorBit = 0x40u;
const uint	kRenderInteriorBit	   = 0x80u;
const float kNearInteriorMarginM   = 1.0e-3;
const float kMaxLandWarpM		   = 1.5;
const int	kWarpFineOctaves	   = 4;
const int	kWarpLowOctaves		   = 2;
const float kWarpLowGain		   = 0.5;
const float kWarpNoisePhaseM	   = 0.375;
const float kNoBoundary			   = 1.0e9;

// The tables below are nibbles of constants, not const arrays: an array indexed at
// run time is copied to local memory in every fragment.

/// D13, lowest first (level 0 .. 8): Mud, Sand, Dirt, GrassShort, Grass,
/// GrassMeadow, GrassTall, Rock, Snow.
uint surfaceOfLevel(int level) {
	return level < 8 ? (0x37908126u >> uint(level * 4)) & 15u : 5u;
}

/// The inverse, by surface id: level 0 .. 8 (Water, never painted, -1).
int levelOfSurface(uint surfaceId) {
	uint nibble = surfaceId < 8u ? (0x71908235u >> (surfaceId * 4u)) & 15u : (0x64u >> ((surfaceId - 8u) * 4u)) & 15u;
	return int(nibble) - 1;
}

// ============================================================================
// THE FIELD
// ============================================================================

/// Render tile at chunk-local tile coordinates (-3 .. 514).
uvec2 renderTile(ivec2 localTile) {
	return texelFetch(u_tileData, localTile + kRenderApronTiles, 0).rg;
}

/// The point the noise reads: kWarpNoisePhaseM off the tile grid, carried into the
/// whole part so the offset stays in [0, 1).
void noisePoint(ivec2 world, vec2 frac, out ivec2 whole, out vec2 offset) {
	vec2  phased = frac + kWarpNoisePhaseM;
	bvec2 carry	 = greaterThanEqual(phased, vec2(1.0));
	whole		 = world + ivec2(carry);
	offset		 = phased - vec2(carry);
}

/// Tile-center lattice: index k sits at k + 0.5, so a point T + f lies in the cell
/// starting at T - 1 when f < 0.5, else at T.
void latticeRead(int tile, float f, out int cell, out float t) {
	if (f < 0.5) {
		cell = tile - 1;
		t	 = f + 0.5;
	} else {
		cell = tile;
		t	 = f - 0.5;
	}
}

float landBilinear(float v00, float v10, float v01, float v11, vec2 t) {
	float bottom = v00 + (v10 - v00) * t.x;
	float top	 = v01 + (v11 - v01) * t.x;
	return bottom + (top - bottom) * t.y;
}

/// One surface's indicator over the 4x4 block, bit j * 4 + i.
bool blockBit(uint mask, int i, int j) {
	return ((mask >> uint(j * 4 + i)) & 1u) != 0u;
}

/// Bits 0..7 set where the eight nibbles of `nibbles` equal `surface`.
uint nibbleMatches(uint nibbles, uint surface) {
	uint x = nibbles ^ (surface * 0x11111111u);
	uint z = ~(x | (x >> 1u) | (x >> 2u) | (x >> 3u)) & 0x11111111u;
	z	   = (z | (z >> 3u)) & 0x03030303u;
	z	   = (z | (z >> 6u)) & 0x000F000Fu;
	return (z | (z >> 12u)) & 0xFFu;
}

/// Three bits of row `row` from column `c`, weighted 1 2 1: nibble n of
/// 0x43213210 is that sum for the bits n.
uint binomialRow(uint mask, int row, int c) {
	return (0x43213210u >> (((mask >> uint(row * 4 + c)) & 7u) * 4u)) & 15u;
}

/// D16 step 1 at a block tile: its 3x3 binomial sum of the indicator over 16, then
/// the thin-feature guard where `thin` (both neighbors along either axis on the
/// other side). The sums are guardedBlur's in SurfaceField.cpp, gathered by row.
float guardedBlur(uint sum, bool inside, bool thin) {
	float blur = smallToFloat(sum) * 0.0625;
	return thin ? (inside ? max(blur, u_landThinFloor) : min(blur, u_landThinCeil)) : blur;
}

/// One surface's 3x3 binomial sums at the block's inner tiles (1, 1), (2, 1),
/// (1, 2) and (2, 2), from its rows' sums, left (columns 0-2) and right (1-3).
uvec4 blockSums(uint mask) {
	uint l0 = binomialRow(mask, 0, 0);
	uint r0 = binomialRow(mask, 0, 1);
	uint l1 = binomialRow(mask, 1, 0);
	uint r1 = binomialRow(mask, 1, 1);
	uint l2 = binomialRow(mask, 2, 0);
	uint r2 = binomialRow(mask, 2, 1);
	uint l3 = binomialRow(mask, 3, 0);
	uint r3 = binomialRow(mask, 3, 1);
	return uvec4(l0 + 2u * l1 + l2, r0 + 2u * r1 + r2, l1 + 2u * l2 + l3, r1 + 2u * r2 + r3);
}

/// The thin test for every bit of a mask at once: both neighbors along either axis
/// on the other side.
uint blockThin(uint mask) {
	return (((mask << 1u) ^ mask) & ((mask >> 1u) ^ mask)) | (((mask << 4u) ^ mask) & ((mask >> 4u) ^ mask));
}

/// The field at lattice offset t, from a surface's inner sums, mask and thin bits.
float blockField(uvec4 sums, uint mask, uint thin, vec2 t) {
	float v00 = guardedBlur(sums.x, blockBit(mask, 1, 1), blockBit(thin, 1, 1));
	float v10 = guardedBlur(sums.y, blockBit(mask, 2, 1), blockBit(thin, 2, 1));
	float v01 = guardedBlur(sums.z, blockBit(mask, 1, 2), blockBit(thin, 1, 2));
	float v11 = guardedBlur(sums.w, blockBit(mask, 2, 2), blockBit(thin, 2, 2));
	return landBilinear(v00, v10, v01, v11, t);
}

/// Meters from the painted boundary, from the painted surface's lead over the
/// surface that would take over (evaluateLand). Across a straight edge between two
/// surfaces the lead is F - 0.5, which runs linear to 0.25 over the first half
/// meter and half as steep over the next meter, to 0.5 at 1.5 m, and this inverts
/// that, so it is exact there, near it on corners and diagonals, and continuous
/// everywhere. (F - 0.5) / |grad F| jumps where the bilinear cell changes, half a
/// meter out on a straight edge, which a band wider than that shows as a cut
/// (ground-textures.md, "Band cap").
float leadDistance(float lead) {
	return 4.0 * lead - 2.0 * clamp(lead, -0.25, 0.25);
}

struct LandEval {
	uint  painted;	// surface id
	float field;	// its field
	vec2  warp;		// the warp offset, meters
	float margin;	// SurfaceFieldSample::margin
	uint  across;	// the surface across the nearest boundary, `painted` when none
	float distance; // meters to that boundary
	float along;	// the fine warp noise's x in [-1, 1], which the contest's threshold follows
};

/// One surface's field alone, as a block of only it reads: 1, no boundary.
LandEval oneSurface(uint surface) {
	LandEval e;
	e.painted  = surface;
	e.field	   = 1.0;
	e.warp	   = vec2(0.0);
	e.margin   = 0.5;
	e.across   = surface;
	e.distance = kNoBoundary;
	e.along	   = 0.0;
	return e;
}

/// In a near-interior tile, whether a read the fine term can move at most `reach`
/// from `at` (an offset from the tile) keeps its block inside the tile's one-surface
/// reach (evaluateSurfaceField has the reasoning).
bool nearInteriorHolds(vec2 at, float reach) {
	return all(greaterThanEqual(at, vec2(reach - 0.5 + kNearInteriorMarginM))) && all(lessThan(at, vec2(1.5 - reach - kNearInteriorMarginM)));
}

/// The field at a point, its fine warp summed over `fineOctaves` of
/// kWarpFineOctaves (all of them for the exact field placement evaluates).
LandEval evaluateLand(ivec2 tile, vec2 frac, int fineOctaves) {
	uvec2 own = renderTile(tile);
	if ((own.g & kRenderInteriorBit) != 0u) {
		return oneSurface(own.r);
	}

	// Warp amplitudes: bilinear over the tile centers around the point, each
	// tile's from its edge surface.
	int	  ax;
	int	  ay;
	vec2  at;
	latticeRead(tile.x, frac.x, ax, at.x);
	latticeRead(tile.y, frac.y, ay, at.y);
	int	  e00	  = int(renderTile(ivec2(ax, ay)).g & kRenderEdgeSurfaceMask);
	int	  e10	  = int(renderTile(ivec2(ax + 1, ay)).g & kRenderEdgeSurfaceMask);
	int	  e01	  = int(renderTile(ivec2(ax, ay + 1)).g & kRenderEdgeSurfaceMask);
	int	  e11	  = int(renderTile(ivec2(ax + 1, ay + 1)).g & kRenderEdgeSurfaceMask);
	float fineAmp = landBilinear(u_landWarpFine[e00], u_landWarpFine[e10], u_landWarpFine[e01], u_landWarpFine[e11], at);
	float lowAmp  = landBilinear(u_landWarpLow[e00], u_landWarpLow[e10], u_landWarpLow[e01], u_landWarpLow[e11], at);

	ivec2 whole;
	vec2  offset;
	noisePoint(u_chunkTileOrigin + tile, frac, whole, offset);
	// Each term's noise pair is the offset's x and y.
	vec2 lowNoise = clamp(
		fractalNoise2SplitPair(
			whole, offset, u_landWarpWavelength.y, u_landWarpInvWavelength.y, u_landWarpSeeds.y, kWarpLowOctaves, kWarpLowGain,
			kWarpLowOctaves
		),
		-1.0,
		1.0
	);

	// A near-interior tile whose read the fine term can't carry out of its one-surface
	// reach: where the low term puts the read, and again once the first fine octave is
	// in, against what the other octaves can add.
	bool nearInterior = (own.g & kRenderNearInteriorBit) != 0u;
	vec2 lowAt		  = frac + lowAmp * lowNoise;
	if (nearInterior && nearInteriorHolds(lowAt, fineAmp)) {
		return oneSurface(own.r);
	}

	// fractalNoise2SplitPair's fine sum an octave at a time; `total` is its weight over
	// every octave.
	SplitNoisePoint finePoint = splitNoisePoint(whole, offset, u_landWarpWavelength.x);
	float			total	  = 0.0;
	float			amp		  = 1.0;
	for (int i = 0; i < kWarpFineOctaves; ++i) {
		total += amp;
		amp *= u_landWarpFineGain;
	}
	vec2  value = vec2(0.0);
	float norm	= 0.0;
	amp			= 1.0;
	for (int i = 0; i < kWarpFineOctaves; ++i) {
		if (i < fineOctaves) {
			value += splitNoiseOctave(finePoint, u_landWarpInvWavelength.x, u_landWarpSeeds.x, i) * amp;
		}
		norm += amp;
		amp *= u_landWarpFineGain;
		if (i == 0 && nearInterior && nearInteriorHolds(lowAt + fineAmp * value / total, fineAmp * (total - norm) / total)) {
			return oneSurface(own.r);
		}
	}
	vec2 fineNoise = clamp(value / norm, -1.0, 1.0);

	LandEval e;
	e.warp	= clamp(fineAmp * fineNoise + lowAmp * lowNoise, -kMaxLandWarpM, kMaxLandWarpM);
	e.along = fineNoise.x;

	// The warped read position, still a whole tile plus an offset.
	vec2 s = frac + e.warp;
	vec2 w = floor(s);
	int	 cx;
	int	 cy;
	vec2 t;
	latticeRead(tile.x + roundToInt(w.x), s.x - w.x, cx, t.x);
	latticeRead(tile.y + roundToInt(w.y), s.y - w.y, cy, t.y);

	// The 4x4 block's surface ids as nibbles, rows 0-1 in `lo` and rows 2-3 in `hi`,
	// gathered a 2x2 quad at a time: four texture instructions where fetching each
	// tile took sixteen. A gather's x, y, z and w are its quad's tiles (0, 1), (1, 1),
	// (1, 0) and (0, 0), and it is centered on the quad's shared corner.
	vec2  corner = vec2(ivec2(cx, cy) + kRenderApronTiles) * kRenderTilesInvSide;
	uvec4 q00	 = textureGather(u_tileData, corner);
	uvec4 q20	 = textureGather(u_tileData, corner + vec2(2.0 * kRenderTilesInvSide, 0.0));
	uvec4 q02	 = textureGather(u_tileData, corner + vec2(0.0, 2.0 * kRenderTilesInvSide));
	uvec4 q22	 = textureGather(u_tileData, corner + vec2(2.0 * kRenderTilesInvSide));
	uint  lo	 = q00.w | q00.z << 4u | q20.w << 8u | q20.z << 12u | q00.x << 16u | q00.y << 20u | q20.x << 24u | q20.y << 28u;
	uint  hi	 = q02.w | q02.z << 4u | q22.w << 8u | q22.z << 12u | q02.x << 16u | q02.y << 20u | q22.x << 24u | q22.y << 28u;
	uint  present = (1u << q00.x) | (1u << q00.y) | (1u << q00.z) | (1u << q00.w) | (1u << q20.x) | (1u << q20.y) | (1u << q20.z) |
				   (1u << q20.w) | (1u << q02.x) | (1u << q02.y) | (1u << q02.z) | (1u << q02.w) | (1u << q22.x) | (1u << q22.y) |
				   (1u << q22.z) | (1u << q22.w);

	// One pass, low to high. The highest surface whose field passes 0.5 paints;
	// where none does (a junction of three or more), the largest field, ties to the
	// higher. Alongside, for the edge look, the painted surface P's lead over the
	// surface that would take over, 0 on the painted boundary and continuous across
	// it: P beats a lower C while it passes or leads, max((fP - fC) / 2, fP - 0.5),
	// and a higher C while C doesn't pass and P leads, min((fP - fC) / 2, 0.5 - fC),
	// so on each side the largest field is the nearest. Between two surfaces both
	// read F - 0.5. Where three meet, no surface's own isoline is the boundary.
	// Scalars only, no per-surface arrays.
	bool  passed	   = false;
	int	  paintedLevel = 0;
	float paintedField = 0.0;
	int	  belowLevel   = -1; // the largest field under the painted surface
	float belowField   = -1.0;
	int	  aboveLevel   = -1; // the largest field over it
	float aboveField   = -1.0;
	int	  bestLevel	   = 0; // until it takes the current level, the largest under it
	float bestField	   = -1.0;
	int	  secondLevel  = -1;
	float secondField  = -1.0;
	e.margin		   = kNoBoundary;
	// Where the block holds exactly two surfaces, as most edges do, the second's
	// inputs are the first's complement: every tile is one or the other, so its
	// sums are 16 less the first's, its indicator the inverse, and its thin bits,
	// which compare each tile with its neighbors, the same. The same integers into
	// the same float steps, so the field is bit for bit what its own mask gives.
	uint  others	= present & (present - 1u);
	bool  twoOnly	= others != 0u && (others & (others - 1u)) == 0u;
	bool  seenFirst = false;
	uvec4 sums		= uvec4(0u);
	uint  mask		= 0u;
	uint  thin		= 0u;
	for (int level = 0; level < 9; ++level) {
		uint surface = surfaceOfLevel(level);
		if ((present & (1u << surface)) == 0u) {
			continue;
		}
		if (twoOnly && seenFirst) {
			sums = uvec4(16u) - sums;
			mask = ~mask;
		} else {
			mask = nibbleMatches(lo, surface) | (nibbleMatches(hi, surface) << 8u);
			sums = blockSums(mask);
			thin = blockThin(mask);
		}
		seenFirst = true;
		float f	  = blockField(sums, mask, thin, t);
		e.margin  = min(e.margin, abs(f - 0.5));
		if (f > 0.5) {
			passed		 = true;
			paintedLevel = level;
			paintedField = f;
			belowLevel	 = bestField >= 0.0 ? bestLevel : -1;
			belowField	 = bestField;
			aboveLevel	 = -1;
			aboveField	 = -1.0;
		} else if (f > aboveField) {
			aboveLevel = level;
			aboveField = f;
		}
		if (f >= bestField) {
			secondField = bestField;
			secondLevel = bestField >= 0.0 ? bestLevel : -1;
			bestField	= f;
			bestLevel	= level;
		} else if (f >= secondField) {
			secondField = f;
			secondLevel = level;
		}
	}

	int	  acrossLevel = -1;
	float lead		  = 0.0;
	if (passed) {
		float belowLead = max(0.5 * (paintedField - belowField), paintedField - 0.5);
		float aboveLead = min(0.5 * (paintedField - aboveField), 0.5 - aboveField);
		bool  fromAbove = aboveLevel >= 0 && (belowLevel < 0 || aboveLead < belowLead);
		acrossLevel		= fromAbove ? aboveLevel : belowLevel;
		lead			= fromAbove ? aboveLead : belowLead;
	} else {
		// All fields at most 0.5, so the lead over either side is half the gap to
		// the second largest.
		paintedLevel = bestLevel;
		paintedField = bestField;
		e.margin	 = min(e.margin, bestField - secondField);
		acrossLevel	 = secondLevel;
		lead		 = 0.5 * (bestField - secondField);
	}
	e.painted  = surfaceOfLevel(paintedLevel);
	e.field	   = paintedField;
	e.across   = acrossLevel >= 0 ? surfaceOfLevel(acrossLevel) : e.painted;
	e.distance = acrossLevel >= 0 ? leadDistance(lead) : kNoBoundary;
	return e;
}

// ============================================================================
// THE LOOK
// ============================================================================

// Ground tile variants are mixed on a hex lattice (Mikkelsen 2022, practical
// hex-tiling) in chunk-local meters: 4 m columns and 148 rows a chunk, so it
// repeats every 512 m and lines up across chunk borders. Vertex v (skewed ids)
// sits at (4 v.x + 2 v.y, kGroundHexRowM v.y); its world id is v plus
// u_groundCellOrigin, (128 cx - 74 cy, 148 cy) for chunk (cx, cy).
const float kGroundHexColM	  = 4.0;
const float kGroundHexRowM	  = 512.0 / 148.0;
const float kGroundTopWeight  = 0.98; // a cell holding this much takes the one sample
const float kGroundMacroSide  = 257.0;

struct HexCells {
	vec2 v1; // skewed ids, whole numbers
	vec2 v2;
	vec2 v3;
	vec3 w; // barycentric weights of v1, v2, v3
};

HexCells groundHexCells(vec2 local) {
	float	 sv	  = local.y * (1.0 / kGroundHexRowM);
	vec2	 sk	  = vec2(local.x * (1.0 / kGroundHexColM) - 0.5 * sv, sv);
	vec2	 base = floor(sk);
	vec2	 f	  = sk - base;
	float	 z	  = 1.0 - f.x - f.y;
	float	 s	  = z < 0.0 ? 1.0 : 0.0;
	float	 s2	  = 2.0 * s - 1.0;
	HexCells c;
	c.w	 = max(vec3(-z * s2, s - f.y * s2, s - f.x * s2), vec3(0.0));
	c.v1 = base + vec2(s, s);
	c.v2 = base + vec2(s, 1.0 - s);
	c.v3 = base + vec2(1.0 - s, s);
	return c;
}

vec4 groundFetch(int array, vec3 at, float lod) {
	if (array == 0) {
		return textureLod(u_groundTiles0, at, lod);
	}
	if (array == 1) {
		return textureLod(u_groundTiles1, at, lod);
	}
	if (array == 2) {
		return textureLod(u_groundTiles2, at, lod);
	}
	return textureLod(u_groundTiles3, at, lod);
}

/// The variant of a cell hashed `h`: h % count, in float on h's low 12 bits (an
/// integer divide by a uniform goes through the conversion unit). `pick` is
/// (1 / count, 0.5 / count): the quotient floors from half a step past a whole
/// number, and the floor keeps roundToInt's bias from folding into the sum. The
/// same as h % count for every power-of-two count up to 4096, within 1 / 4096 of
/// uniform otherwise.
uint groundVariant(uint h, int count, vec2 pick) {
	uint x = h & 0xFFFu;
	return x - uint(count * roundToInt(floor(smallToFloat(x) * pick.x + pick.y)));
}

/// `bits` (0..255) / 256, from a mantissa in [1, 2): a sum or product a compiler
/// reorders around it rounds at that scale, not at the whole-number bias's.
float byteFraction(uint bits) {
	return uintBitsToFloat(0x3F800000u | bits << 15u) - 1.0;
}

/// A surface's tile as hex cell `v` lays it at `local`, linear, its height in
/// alpha: the cell's variant, rotation (by the surface's rotate) and offset hash
/// from its world id and the surface's seed group, so a co-baked family picks
/// alike and its layouts meet across the family's edges. Variants are picked
/// uniformly; weighted picks (mostly quiet, a few busy) wait for a ground def that
/// asks for them.
vec4 groundCellSample(uint surface, vec2 local, vec2 v) {
	ivec4 layers = u_groundLayers[surface];
	vec4  params = u_groundParams[surface];
	int	  rotate = layers.w & 3;
	ivec2 id	 = ivec2(roundToInt(v.x), roundToInt(v.y)) + u_groundCellOrigin;
	uint  h		 = noiseHash3(id.x, id.y, layers.w >> 2, u_groundSeed);
	vec2  center = vec2(v.x * kGroundHexColM + v.y * (0.5 * kGroundHexColM), v.y * kGroundHexRowM);
	vec2  d		 = (local - center) * params.x;
	vec2  cs	 = vec2(1.0, 0.0);
	if (rotate == 0) {
		float angle = byteFraction((h >> 8u) & 0xFFu) * 6.2831853;
		cs			= vec2(cos(angle), sin(angle));
	} else if (rotate == 1) {
		uint q = (h >> 8u) & 3u;
		cs	   = q == 0u ? vec2(1.0, 0.0) : (q == 1u ? vec2(0.0, 1.0) : (q == 2u ? vec2(-1.0, 0.0) : vec2(0.0, -1.0)));
	} else if (rotate == 3 && (h & 0x100u) != 0u) {
		d.x = -d.x; // mirror: flip about the cell's center, no turn; the angle's bit is free here
	}
	vec2  uv	= vec2(cs.x * d.x - cs.y * d.y, cs.y * d.x + cs.x * d.y) + vec2(byteFraction((h >> 16u) & 0xFFu), byteFraction(h >> 24u));
	float layer = smallToFloat(uint(layers.y) + groundVariant(h, layers.z, params.zw));
	return groundFetch(layers.x, vec3(uv, layer), params.y);
}

/// Mikkelsen's gain on blend weights: more contrast for k > 1.
vec3 groundGain(vec3 w, float k) {
	vec3 s = 2.0 * step(0.5, w);
	vec3 m = 2.0 * (1.0 - s);
	vec3 r = 0.5 * s + 0.25 * m * pow(max(vec3(0.0), s + w * m), vec3(k));
	return r / (r.x + r.y + r.z);
}

/// A surface's linear ground color at `local`, its height in alpha: the hex cells'
/// samples, weighted by their sharpened barycentric weights times 0.4 + 0.6 times
/// the baked height (Mikkelsen weighs by luminance; the height keeps what stands
/// tallest, tuft tops, across a cell seam), then the zoom's gain. One sample, the
/// dominant cell's, where a cell holds kGroundTopWeight or below the hex cutoff.
vec4 groundTexture(uint surface, vec2 local) {
	HexCells c = groundHexCells(local);
	if (!u_groundHex) {
		vec2 v = c.w.x >= c.w.y && c.w.x >= c.w.z ? c.v1 : (c.w.y >= c.w.z ? c.v2 : c.v3);
		return groundCellSample(surface, local, v);
	}
	vec3 w = pow(c.w, vec3(u_groundBlendContrast));
	w /= w.x + w.y + w.z;

	// The heaviest cell first (ties to v1, then v2) and the other two after it, so
	// every pixel takes its first sample from the same instructions and only the
	// transition bands branch for more: a warp straddling a band edge runs the one
	// sample once.
	bool first1 = w.x >= w.y && w.x >= w.z;
	bool first2 = !first1 && w.y >= w.z;
	vec2 vA		= first1 ? c.v1 : (first2 ? c.v2 : c.v3);
	vec2 vB		= first1 ? c.v2 : c.v1;
	vec2 vC		= first1 || first2 ? c.v3 : c.v2;
	vec3 ws		= first1 ? w : (first2 ? w.yxz : w.zxy);
	vec4 cA		= groundCellSample(surface, local, vA);
	if (ws.x >= kGroundTopWeight) {
		return cA;
	}

	// A transition band: the cells holding more than the heaviest one leaves over.
	float floorW = 1.0 - kGroundTopWeight;
	vec4  cB	 = ws.y > floorW ? groundCellSample(surface, local, vB) : vec4(0.0);
	vec4  cC	 = ws.z > floorW ? groundCellSample(surface, local, vC) : vec4(0.0);
	ws *= vec3(greaterThan(ws, vec3(floorW))) * (0.4 + 0.6 * vec3(cA.a, cB.a, cC.a));
	ws /= ws.x + ws.y + ws.z;
	if (u_groundBlendGainK != 1.0) {
		ws = groundGain(ws, u_groundBlendGainK);
	}
	return ws.x * cA + ws.y * cB + ws.z * cC;
}

vec3 groundEncodeSrgb(vec3 linear) {
	vec3 low  = linear * 12.92;
	vec3 high = 1.055 * pow(max(linear, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
	return mix(low, high, step(vec3(0.0031308), linear));
}

/// A surface's macro tint and value at this pixel (`macro`, its sample of
/// u_groundMacro), multiplied into its sRGB color.
vec3 groundTint(uint surface, vec4 macro) {
	vec3  tint	= u_groundTintBase[surface] + u_groundTintLush[surface] * macro.r;
	float value = u_groundMacroValue.x + macro.g * u_groundMacroValue.y + macro.b * u_groundMacroValue.z;
	return tint * value;
}

/// The ground color at chunk-local position `local`, in sRGB. Where an edge's band
/// reaches, its two surfaces contest by height (ground-textures.md, "The contest"):
/// b runs -1 to 1 across the band, the upper surface's baked height against the
/// lower's decides each texel, the window softens the decision, and the breakup
/// moves the threshold along the edge with the field's fine warp noise. At |b| = 1
/// the result is exactly one surface, so outside the band a pixel takes one sample.
vec3 landColor(vec2 local) {
	vec2	 whole	 = clamp(floor(local), vec2(0.0), vec2(511.0));
	ivec2	 tile	 = ivec2(roundToInt(whole.x), roundToInt(whole.y));
	vec2	 frac	 = local - whole;
	LandEval e		 = evaluateLand(tile, frac, u_landWarpFineOctaves);
	vec4	 macro	 = textureLod(u_groundMacro, (local * 0.5 + 0.5) * (1.0 / kGroundMacroSide), 0.0);
	vec4	 painted = groundTexture(e.painted, local);
	if (e.across != e.painted) {
		int	  paintedLevel	 = levelOfSurface(e.painted);
		int	  acrossLevel	 = levelOfSurface(e.across);
		bool  paintedIsUpper = paintedLevel > acrossLevel;
		int	  upperLevel	 = paintedIsUpper ? paintedLevel : acrossLevel;
		int	  pair			 = ((upperLevel * (upperLevel - 1)) >> 1) + (paintedIsUpper ? acrossLevel : paintedLevel);
		vec4  band			 = u_landPair[2 * pair];
		float sigma			 = paintedIsUpper ? e.distance : -e.distance; // positive into the upper surface
		float bu			 = clamp(sigma * band.x + band.y, 0.0, 1.0); // (b + 1) / 2
		if (bu > 0.0 && bu < 1.0) {
			vec4  shade	  = u_landPair[2 * pair + 1];
			vec4  other	  = groundTexture(e.across, local);
			vec4  upper	  = paintedIsUpper ? painted : other;
			vec4  lower	  = paintedIsUpper ? other : painted;
			uint  upperId = paintedIsUpper ? e.painted : e.across;
			uint  lowerId = paintedIsUpper ? e.across : e.painted;
			float b		  = 2.0 * bu - 1.0;
			float s		  = clamp((upper.a - lower.a) * band.z + b * (band.z + band.w + 0.5) + e.along * band.w + 0.5, 0.0, 1.0);
			// The shading terms, each nothing at |b| = 1: the tint veils the lower surface
			// with the upper's color, the stain darkens the upper toward the edge, the
			// contact shadow darkens what shows of the lower under the upper's edge, and
			// the ridge lightens a line near the upper's outer reach.
			float a		 = s + shade.w * (1.0 - s) * clamp(1.0 + b, 0.0, 1.0);
			float stain	 = 1.0 - shade.y * (1.0 - smoothstep(0.0, 1.0, b));
			float light	 = (1.0 - shade.x * (1.0 - s) * clamp(1.0 + 4.0 * b, 0.0, 1.0)) * (1.0 + shade.z * clamp(1.0 - 5.0 * abs(b - 0.8), 0.0, 1.0));
			// Mixed in linear light, the way the hex blend mixes, and encoded once; the
			// macro tints, which multiply the sRGB color, mix by the same weight.
			vec3 linear = mix(lower.rgb, upper.rgb * stain, a) * light;
			return groundEncodeSrgb(linear) * mix(groundTint(lowerId, macro), groundTint(upperId, macro), a);
		}
	}
	return groundEncodeSrgb(painted.rgb) * groundTint(e.painted, macro);
}
