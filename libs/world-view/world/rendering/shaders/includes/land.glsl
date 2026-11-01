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
// mip baked from the vectors. Per surface id:
//   u_groundLayers: x array (0..3), y first layer, z layer count, w rotate (0 free, 1 quarter, 2 none, 3 mirror)
//   u_groundParams: x 1 / period (m), y mip level this draw, z macro tint amount
//   u_groundDry, u_groundLush: the macro tints, multiplied into the sRGB color
uniform sampler2DArray u_groundTiles0;
uniform sampler2DArray u_groundTiles1;
uniform sampler2DArray u_groundTiles2;
uniform sampler2DArray u_groundTiles3;
uniform ivec4		   u_groundLayers[10];
uniform vec4		   u_groundParams[10];
uniform vec3		   u_groundDry[10];
uniform vec3		   u_groundLush[10];
uniform uint		   u_groundSeed;
uniform bool		   u_groundHex;			   // px/m at or over terrain/ground/hexMinPxPerMeter
uniform float		   u_groundBlendContrast;  // exponent on the barycentric weights
uniform float		   u_groundBlendGainK;	   // log(1 - r) / log(0.5) for the zoom's gain r; 1 is none
uniform float		   u_groundMacroMidAmp;	   // value swing of the macro's mid octave
uniform float		   u_groundMacroFineAmp;   // and of its fine one

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

// The look, all tunables (ChunkRenderer.cpp): a sparse fringe of the upper surface
// on the lower side of each edge, its width varying along the edge (D15) with the
// fine warp noise, and a rim just inside the upper surface. Meters.
uniform float u_landFringeW[10];
uniform float u_landRimDark[10];
uniform float u_landFringeOpacity;
uniform float u_landFringeAlongAmp;
uniform float u_landRimW;
uniform int	  u_landBreakupWavelength; // whole meters
uniform float u_landBreakupInvWavelength;
uniform uint  u_landBreakupSeed;

// ============================================================================
// CONSTANTS (RenderTiles.h, SurfaceField.h)
// ============================================================================

const int	kRenderApronTiles	   = 3;
const uint	kRenderEdgeSurfaceMask = 0x0Fu;
const uint	kRenderInteriorBit	   = 0x80u;
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

/// D16 step 1 at block tile (i, j): the 3x3 binomial of the indicator, then the
/// thin-feature guard (both neighbors along either axis on the other side).
float guardedBlur(uint mask, int i, int j) {
	int sum = 0;
	sum += blockBit(mask, i - 1, j - 1) ? 1 : 0;
	sum += blockBit(mask, i, j - 1) ? 2 : 0;
	sum += blockBit(mask, i + 1, j - 1) ? 1 : 0;
	sum += blockBit(mask, i - 1, j) ? 2 : 0;
	sum += blockBit(mask, i, j) ? 4 : 0;
	sum += blockBit(mask, i + 1, j) ? 2 : 0;
	sum += blockBit(mask, i - 1, j + 1) ? 1 : 0;
	sum += blockBit(mask, i, j + 1) ? 2 : 0;
	sum += blockBit(mask, i + 1, j + 1) ? 1 : 0;
	float blur	 = float(sum) * 0.0625;
	bool  inside = blockBit(mask, i, j);
	bool  thin	 = (blockBit(mask, i - 1, j) != inside && blockBit(mask, i + 1, j) != inside) ||
				  (blockBit(mask, i, j - 1) != inside && blockBit(mask, i, j + 1) != inside);
	if (!thin) {
		return blur;
	}
	return inside ? max(blur, u_landThinFloor) : min(blur, u_landThinCeil);
}

/// The field at lattice offset t (x) and its gradient per meter (yz), for the
/// distance to its isoline.
vec3 blockField(uint mask, vec2 t) {
	float v00 = guardedBlur(mask, 1, 1);
	float v10 = guardedBlur(mask, 2, 1);
	float v01 = guardedBlur(mask, 1, 2);
	float v11 = guardedBlur(mask, 2, 2);
	float bottom = v00 + (v10 - v00) * t.x;
	float top	 = v01 + (v11 - v01) * t.x;
	vec2  grad	 = vec2((1.0 - t.y) * (v10 - v00) + t.y * (v11 - v01), top - bottom);
	return vec3(bottom + (top - bottom) * t.y, grad);
}

struct LandEval {
	uint  painted;	// surface id
	float field;	// its field
	vec2  warp;		// the warp offset, meters
	float margin;	// SurfaceFieldSample::margin
	uint  across;	// the surface across the nearest boundary, `painted` when none
	float distance; // meters to that boundary
	float along;	// the fine warp noise's x in [-1, 1], which the fringe width follows
};

/// The field at a point, its fine warp summed over `fineOctaves` of
/// kWarpFineOctaves (all of them for the exact field placement evaluates).
LandEval evaluateLand(ivec2 tile, vec2 frac, int fineOctaves) {
	LandEval e;
	uvec2	 own = renderTile(tile);
	if ((own.g & kRenderInteriorBit) != 0u) {
		e.painted  = own.r;
		e.field	   = 1.0;
		e.warp	   = vec2(0.0);
		e.margin   = 0.5;
		e.across   = own.r;
		e.distance = kNoBoundary;
		e.along	   = 0.0;
		return e;
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
	vec2 fineNoise = clamp(
		fractalNoise2SplitPair(
			whole, offset, u_landWarpWavelength.x, u_landWarpInvWavelength.x, u_landWarpSeeds.x, kWarpFineOctaves, u_landWarpFineGain,
			fineOctaves
		),
		-1.0,
		1.0
	);
	vec2 lowNoise = clamp(
		fractalNoise2SplitPair(
			whole, offset, u_landWarpWavelength.y, u_landWarpInvWavelength.y, u_landWarpSeeds.y, kWarpLowOctaves, kWarpLowGain,
			kWarpLowOctaves
		),
		-1.0,
		1.0
	);
	e.warp	= clamp(fineAmp * fineNoise + lowAmp * lowNoise, -kMaxLandWarpM, kMaxLandWarpM);
	e.along = fineNoise.x;

	// The warped read position, still a whole tile plus an offset.
	vec2 s = frac + e.warp;
	vec2 w = floor(s);
	int	 cx;
	int	 cy;
	vec2 t;
	latticeRead(tile.x + int(w.x), s.x - w.x, cx, t.x);
	latticeRead(tile.y + int(w.y), s.y - w.y, cy, t.y);

	// The 4x4 block's surface ids as nibbles, rows 0-1 in `lo` and rows 2-3 in `hi`.
	uint lo		 = 0u;
	uint hi		 = 0u;
	uint present = 0u;
	for (int j = 0; j < 4; ++j) {
		for (int i = 0; i < 4; ++i) {
			uint id	   = renderTile(ivec2(cx - 1 + i, cy - 1 + j)).r;
			uint shift = uint(((j & 1) * 4 + i) * 4);
			lo |= j < 2 ? id << shift : 0u;
			hi |= j < 2 ? 0u : id << shift;
			present |= 1u << id;
		}
	}

	// One pass, low to high. The highest surface whose field passes 0.5 paints;
	// where none does (a junction of three or more), the largest field, ties to the
	// higher. Alongside, the nearest boundary of the painted region for
	// anti-aliasing and the edge look: the isoline of every surface that would take
	// over by passing 0.5 (those above the painted one; all others where nothing
	// passes), and the painted surface's own, past which the passing surface below
	// it shows, else the largest other field. Scalars only, no per-surface arrays.
	bool  passed	   = false;
	int	  paintedLevel = 0;
	float paintedField = 0.0;
	float paintedDist  = kNoBoundary;
	int	  belowLevel   = -1; // the passing surface just under the painted one
	float aboveDist	   = kNoBoundary;
	int	  aboveLevel   = -1; // nearest isoline among the surfaces above it
	float nearDist	   = kNoBoundary;
	int	  nearLevel	   = -1;
	float nextDist	   = kNoBoundary;
	int	  nextLevel	   = -1;
	int	  bestLevel	   = 0;
	float bestField	   = -1.0;
	float bestDist	   = kNoBoundary;
	int	  secondLevel  = -1;
	float secondField  = -1.0;
	e.margin		   = kNoBoundary;
	for (int level = 0; level < 9; ++level) {
		uint surface = surfaceOfLevel(level);
		if ((present & (1u << surface)) == 0u) {
			continue;
		}
		uint  mask = nibbleMatches(lo, surface) | (nibbleMatches(hi, surface) << 8u);
		vec3  f	   = blockField(mask, t);
		float dist = (f.x - 0.5) / max(length(f.yz), 1.0e-4); // meters, negative until it passes
		e.margin   = min(e.margin, abs(f.x - 0.5));
		if (f.x > 0.5) {
			belowLevel	 = passed ? paintedLevel : -1;
			passed		 = true;
			paintedLevel = level;
			paintedField = f.x;
			paintedDist	 = dist;
			aboveDist	 = kNoBoundary;
			aboveLevel	 = -1;
		} else if (-dist < aboveDist) {
			aboveDist  = -dist;
			aboveLevel = level;
		}
		if (-dist < nearDist) {
			nextDist  = nearDist;
			nextLevel = nearLevel;
			nearDist  = -dist;
			nearLevel = level;
		} else if (-dist < nextDist) {
			nextDist  = -dist;
			nextLevel = level;
		}
		if (f.x >= bestField) {
			secondField = bestField;
			secondLevel = bestField >= 0.0 ? bestLevel : -1;
			bestField	= f.x;
			bestLevel	= level;
			bestDist	= dist;
		} else if (f.x >= secondField) {
			secondField = f.x;
			secondLevel = level;
		}
	}

	int	  acrossLevel = -1;
	float distance	  = kNoBoundary;
	if (passed) {
		acrossLevel = aboveLevel;
		distance	= aboveDist;
		int below	= belowLevel >= 0 ? belowLevel : (bestLevel != paintedLevel ? bestLevel : secondLevel);
		if (below >= 0 && paintedDist < distance) {
			acrossLevel = below;
			distance	= paintedDist;
		}
	} else {
		paintedLevel = bestLevel;
		paintedField = bestField;
		e.margin	 = min(e.margin, bestField - secondField);
		acrossLevel	 = nearLevel != paintedLevel ? nearLevel : nextLevel;
		distance	 = nearLevel != paintedLevel ? nearDist : nextDist;
	}
	e.painted  = surfaceOfLevel(paintedLevel);
	e.field	   = paintedField;
	e.across   = acrossLevel >= 0 ? surfaceOfLevel(acrossLevel) : e.painted;
	e.distance = acrossLevel >= 0 ? distance : kNoBoundary;
	return e;
}

// ============================================================================
// THE LOOK
// ============================================================================

// Ground tile variants are mixed on a hex lattice (Mikkelsen 2022, practical
// hex-tiling) in chunk-local meters: 4 m columns and 148 rows a chunk, so it
// repeats every 512 m and lines up across chunk borders. Vertex v (skewed ids)
// sits at (4 v.x + 2 v.y, kGroundHexRowM v.y); its world id is v plus
// (128 cx - 74 cy, 148 cy) for chunk (cx, cy).
const float kGroundHexColM	  = 4.0;
const float kGroundHexRowM	  = 512.0 / 148.0;
const float kGroundTopWeight  = 0.98; // a cell holding this much takes the one sample
const float kGroundMacroSide  = 257.0;
const vec3	kGroundLuma		  = vec3(0.299, 0.587, 0.114);

struct HexCells {
	ivec2 v1;
	ivec2 v2;
	ivec2 v3;
	vec3  w; // barycentric weights of v1, v2, v3
};

HexCells groundHexCells(vec2 local) {
	float	 sv	  = local.y * (1.0 / kGroundHexRowM);
	vec2	 sk	  = vec2(local.x * (1.0 / kGroundHexColM) - 0.5 * sv, sv);
	ivec2	 base = ivec2(floor(sk));
	vec2	 f	  = sk - vec2(base);
	float	 z	  = 1.0 - f.x - f.y;
	int		 s	  = z < 0.0 ? 1 : 0;
	float	 s2	  = float(2 * s - 1);
	HexCells c;
	c.w	 = max(vec3(-z * s2, float(s) - f.y * s2, float(s) - f.x * s2), vec3(0.0));
	c.v1 = base + ivec2(s, s);
	c.v2 = base + ivec2(s, 1 - s);
	c.v3 = base + ivec2(1 - s, s);
	return c;
}

vec3 groundFetch(int array, vec3 at, float lod) {
	if (array == 0) {
		return textureLod(u_groundTiles0, at, lod).rgb;
	}
	if (array == 1) {
		return textureLod(u_groundTiles1, at, lod).rgb;
	}
	if (array == 2) {
		return textureLod(u_groundTiles2, at, lod).rgb;
	}
	return textureLod(u_groundTiles3, at, lod).rgb;
}

/// A surface's tile as hex cell `v` lays it at `local`, linear: the cell's
/// variant, rotation (by the surface's rotate) and offset hash from its world id.
/// Variants are picked uniformly; weighted picks (mostly quiet, a few busy) wait
/// for a ground def that asks for them.
vec3 groundCellSample(uint surface, vec2 local, ivec2 v) {
	ivec4 layers = u_groundLayers[surface];
	vec4  params = u_groundParams[surface];
	ivec2 chunk	 = u_chunkTileOrigin / 512;
	uint  h		 = noiseHash3(v.x + 128 * chunk.x - 74 * chunk.y, v.y + 148 * chunk.y, int(surface), u_groundSeed);
	vec2  center = vec2(float(v.x) * kGroundHexColM + float(v.y) * (0.5 * kGroundHexColM), float(v.y) * kGroundHexRowM);
	vec2  d		 = (local - center) * params.x;
	vec2  cs	 = vec2(1.0, 0.0);
	if (layers.w == 0) {
		float angle = float((h >> 8u) & 0xFFu) * (6.2831853 / 256.0);
		cs			= vec2(cos(angle), sin(angle));
	} else if (layers.w == 1) {
		uint q = (h >> 8u) & 3u;
		cs	   = q == 0u ? vec2(1.0, 0.0) : (q == 1u ? vec2(0.0, 1.0) : (q == 2u ? vec2(-1.0, 0.0) : vec2(0.0, -1.0)));
	} else if (layers.w == 3 && (h & 0x100u) != 0u) {
		d.x = -d.x; // mirror: flip about the cell's center, no turn; the angle's bit is free here
	}
	vec2  uv	= vec2(cs.x * d.x - cs.y * d.y, cs.y * d.x + cs.x * d.y) + vec2(float((h >> 16u) & 0xFFu), float(h >> 24u)) * (1.0 / 256.0);
	float layer = float(layers.y + int(h % uint(max(layers.z, 1))));
	return groundFetch(layers.x, vec3(uv, layer), params.y);
}

/// Mikkelsen's gain on blend weights: more contrast for k > 1.
vec3 groundGain(vec3 w, float k) {
	vec3 s = 2.0 * step(0.5, w);
	vec3 m = 2.0 * (1.0 - s);
	vec3 r = 0.5 * s + 0.25 * m * pow(max(vec3(0.0), s + w * m), vec3(k));
	return r / (r.x + r.y + r.z);
}

/// A surface's linear ground color at `local`: the hex cells' samples, weighted
/// by their sharpened barycentric weights times 0.4 + 0.6 luminance, then the
/// zoom's gain. One sample where a cell holds kGroundTopWeight, below the hex
/// cutoff, or with `blend` false (the fringe's other surface): the dominant cell.
vec3 groundTexture(uint surface, vec2 local, bool blend) {
	HexCells c = groundHexCells(local);
	if (!blend || !u_groundHex) {
		ivec2 v = c.w.x >= c.w.y && c.w.x >= c.w.z ? c.v1 : (c.w.y >= c.w.z ? c.v2 : c.v3);
		return groundCellSample(surface, local, v);
	}
	vec3 w = pow(c.w, vec3(u_groundBlendContrast));
	w /= w.x + w.y + w.z;
	if (w.x >= kGroundTopWeight) {
		return groundCellSample(surface, local, c.v1);
	}
	if (w.y >= kGroundTopWeight) {
		return groundCellSample(surface, local, c.v2);
	}
	if (w.z >= kGroundTopWeight) {
		return groundCellSample(surface, local, c.v3);
	}
	// A transition band: the cells holding more than the dominant one leaves over.
	float floorW = 1.0 - kGroundTopWeight;
	vec3  c1	 = w.x > floorW ? groundCellSample(surface, local, c.v1) : vec3(0.0);
	vec3  c2	 = w.y > floorW ? groundCellSample(surface, local, c.v2) : vec3(0.0);
	vec3  c3	 = w.z > floorW ? groundCellSample(surface, local, c.v3) : vec3(0.0);
	w *= vec3(greaterThan(w, vec3(floorW))) * (0.4 + 0.6 * vec3(dot(c1, kGroundLuma), dot(c2, kGroundLuma), dot(c3, kGroundLuma)));
	w /= w.x + w.y + w.z;
	if (u_groundBlendGainK != 1.0) {
		w = groundGain(w, u_groundBlendGainK);
	}
	return w.x * c1 + w.y * c2 + w.z * c3;
}

vec3 groundEncodeSrgb(vec3 linear) {
	vec3 low  = linear * 12.92;
	vec3 high = 1.055 * pow(max(linear, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
	return mix(low, high, step(vec3(0.0031308), linear));
}

/// A surface's ground color in sRGB, which the rest of the pass works in, tinted
/// by the macro layer (`macro`, this pixel's sample of u_groundMacro).
vec3 groundColor(uint surface, vec2 local, vec4 macro, bool blend) {
	vec3  color = groundEncodeSrgb(groundTexture(surface, local, blend));
	vec3  tint	= mix(u_groundDry[surface], u_groundLush[surface], macro.r);
	float value = 1.0 + (macro.g * 2.0 - 1.0) * u_groundMacroMidAmp + (macro.b * 2.0 - 1.0) * u_groundMacroFineAmp;
	return color * mix(vec3(1.0), tint, u_groundParams[surface].z) * value;
}

/// The ground color at chunk-local position `local`, anti-aliased over one pixel.
vec3 landColor(vec2 local, float metersPerPixel) {
	ivec2	 tile	 = clamp(ivec2(floor(local)), ivec2(0), ivec2(511));
	vec2	 frac	 = local - vec2(tile);
	LandEval e		 = evaluateLand(tile, frac, u_landWarpFineOctaves);
	vec4	 macro	 = textureLod(u_groundMacro, (local * 0.5 + 0.5) * (1.0 / kGroundMacroSide), 0.0);
	vec3	 painted = groundColor(e.painted, local, macro, true);
	if (e.across == e.painted) {
		return painted;
	}

	// Signed distance to the boundary, positive into the upper surface of the pair.
	bool paintedIsUpper = levelOfSurface(e.painted) > levelOfSurface(e.across);
	uint upper			= paintedIsUpper ? e.painted : e.across;
	uint lower			= paintedIsUpper ? e.across : e.painted;
	float sigma			= paintedIsUpper ? e.distance : -e.distance;

	float fringeReach = u_landFringeW[int(upper)] * (1.0 + u_landFringeAlongAmp);
	if (e.distance >= max(max(fringeReach, u_landRimW), metersPerPixel)) {
		return painted;
	}

	ivec2 whole;
	vec2  offset;
	noisePoint(u_chunkTileOrigin + tile, frac, whole, offset);
	float breakup = smoothstep(
		-0.25,
		0.35,
		fractalNoise2SplitPair(whole, offset, u_landBreakupWavelength, u_landBreakupInvWavelength, u_landBreakupSeed, 2, 0.5, 2).x
	);
	float fringeW = u_landFringeW[int(upper)] * (1.0 + u_landFringeAlongAmp * e.along);
	float fringe  = fringeW > 0.0 ? u_landFringeOpacity * breakup * (1.0 - smoothstep(0.0, fringeW, max(-sigma, 0.0))) : 0.0;
	float rim	  = u_landRimDark[int(upper)] * (1.0 - smoothstep(0.0, u_landRimW, max(sigma, 0.0)));

	// The fringe's other surface takes one sample: the band is thin and a blend wouldn't show.
	vec3 upperColor = paintedIsUpper ? painted : groundColor(upper, local, macro, false);
	vec3 lowerColor = paintedIsUpper ? groundColor(lower, local, macro, false) : painted;
	vec3 lowerSide	= mix(lowerColor, upperColor, fringe);
	vec3 upperSide	= upperColor * (1.0 - rim);
	return mix(lowerSide, upperSide, smoothstep(-0.5 * metersPerPixel, 0.5 * metersPerPixel, sigma));
}
