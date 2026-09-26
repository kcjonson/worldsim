// water.glsl - water and shore painted per pixel from a chunk's terrain distance
// field (docs/technical/organic-terrain/terrain-polygons-architecture.md D10,
// 10.2). Every width is in meters, so the look holds at every zoom. Every u_*
// below the textures is a debug-server tunable (10.5, ChunkRenderer.cpp).
//
// Texture lattices mirror TerrainDistanceField: texel k of spacing s covers
// [k*s, (k+1)*s) in chunk-local meters and is stored at k + 1 (a one-texel
// gutter per side), so a chunk's bilinear footprint never leaves its textures.

// ============================================================================
// TEXTURES (per chunk)
// ============================================================================

uniform bool u_hasWater;            // false: the chunk has no water in reach, skip everything
uniform usampler2D u_sdfTileMap;    // 32x32 R16UI: atlas cell of each 16 m tile's near tile, or 0xFFFF
uniform sampler2D u_sdfNear;        // near tiles (34x34 RGB16F each) in an atlas, u_sdfNearColumns per row
uniform int u_sdfNearColumns;
uniform sampler2D u_sdfFar;         // 258x258 RGB16F, 2 m
uniform sampler2D u_shoreProfile;   // 514x514 RGBA8, 1 m: slope, exposure, sand, mud
uniform sampler2D u_channelFrame;   // 514x514 RGB16F, 1 m, nearest: arc s (mod 64), width ratio, curvature*hw

uniform float u_time;               // seconds
uniform float u_metersPerPixel;     // one screen pixel in world meters at this zoom

// ============================================================================
// TUNABLES (10.5)
// ============================================================================

uniform float u_wobbleAmp;
uniform float u_wobbleWavelength;
uniform float u_alongAmp;
uniform float u_alongWavelength;
uniform float u_drySandW;
uniform float u_wetSandW;
uniform float u_mudW;
uniform float u_bandTail;
uniform float u_wetlandMudGain;
uniform float u_edgeW;
uniform float u_edgeSlopeGain;
uniform float u_edgeMin;
uniform float u_edgeMax;
uniform float u_lodBandM;

uniform float u_shallowsK;
uniform float u_shallowsMin;
uniform float u_shallowsMax;
uniform float u_slopeMin;
uniform float u_midAt;
uniform float u_bedOpacity;
uniform float u_bedFade;
uniform float u_patchWavelength;
uniform float u_patchAmp;
uniform float u_barT0;
uniform float u_barT1;
uniform float u_weedT0;
uniform float u_weedT1;
uniform float u_shimmerAmp;
uniform float u_shimmerFreq;
uniform float u_shimmerDrift;
uniform float u_foamW;
uniform float u_foamWavelength;
uniform float u_foamDrift;

uniform float u_thalwegInner;
uniform float u_thalwegOuter;
uniform float u_thalwegDepth;
uniform float u_riffleWavelength;
uniform float u_riffleAmp;
uniform float u_poolAmp;
uniform float u_flowSpeed;

uniform vec3 u_drySand;
uniform vec3 u_wetSand;
uniform vec3 u_mudBank;
uniform vec3 u_bedGrass;
uniform vec3 u_edgeColor;
uniform vec3 u_waterShallow;
uniform vec3 u_waterMid;
uniform vec3 u_waterDeep;
uniform vec3 u_riffleLight;
uniform vec3 u_barColor;
uniform vec3 u_weedColor;
uniform vec3 u_foam;

// ============================================================================
// LATTICES (TerrainDistanceField)
// ============================================================================

const float kNearTexelM     = 0.5;
const float kNearTileM      = 16.0;
const int   kNearTileTexels = 32;
const int   kNearTileStride = 34;
const uint  kNoNearTile     = 0xFFFFu;
const float kFarTexelM      = 2.0;
const float kDetailTexelM   = 1.0;
const float kNoThalweg      = 2.0;
const float kArcWrapM       = 64.0;
const float kTau            = 6.2831853;

const int kWaterKindOcean   = 0;
const int kWaterKindWetland = 2;

// ============================================================================
// NOISE
// ============================================================================

// Integer lattice hash: stays well distributed at world coordinates in the tens
// of kilometers, where a sin() hash collapses to float noise.
float waterHash(ivec2 cell, uint seed) {
	uvec2 q = uvec2(cell) * uvec2(1597334673u, 3812015801u);
	uint  n = (q.x ^ q.y ^ seed) * 1597334673u;
	n ^= n >> 16u;
	n *= 2246822519u;
	n ^= n >> 13u;
	return float(n >> 8u) * (1.0 / 16777216.0);
}

// Value noise in [-1, 1].
float waterValueNoise(vec2 p, uint seed) {
	vec2  i = floor(p);
	vec2  f = p - i;
	ivec2 c = ivec2(i);
	vec2  u = f * f * (3.0 - 2.0 * f);
	float a = waterHash(c, seed);
	float b = waterHash(c + ivec2(1, 0), seed);
	float e = waterHash(c + ivec2(0, 1), seed);
	float g = waterHash(c + ivec2(1, 1), seed);
	return mix(mix(a, b, u.x), mix(e, g, u.x), u.y) * 2.0 - 1.0;
}

// fBm in [-1, 1]; each octave rotated so the lattice never lines up.
float waterFbm(vec2 p, int octaves, uint seed) {
	const mat2 kRot = mat2(0.8, 0.6, -0.6, 0.8);
	float sum = 0.0;
	float amp = 0.5;
	float norm = 0.0;
	for (int i = 0; i < octaves; ++i) {
		sum += amp * waterValueNoise(p, seed + uint(i) * 101u);
		norm += amp;
		p = kRot * p * 2.0;
		amp *= 0.5;
	}
	return sum / norm;
}

const uint kSeedWobble  = 11u;
const uint kSeedAlong   = 23u;
const uint kSeedPatch   = 37u;
const uint kSeedShimmer = 41u;
const uint kSeedFoam    = 53u;
const uint kSeedStreak  = 67u;

// ============================================================================
// SAMPLING
// ============================================================================

struct TerrainSdf {
	float d;    // signed distance to the shoreline, meters, negative in water
	float g;    // distance to the nearest thalweg / local half-width
	int   kind; // WaterKind of the nearest ring, nearest-filtered
};

/// The near level where the tile map has a near tile, else the far level.
TerrainSdf sampleSdf(vec2 local) {
	ivec2 mapSize = textureSize(u_sdfTileMap, 0);
	ivec2 tile	  = clamp(ivec2(floor(local / kNearTileM)), ivec2(0), mapSize - 1);
	uint  cellIdx = texelFetch(u_sdfTileMap, tile, 0).r;

	TerrainSdf s;
	if (cellIdx != kNoNearTile) {
		ivec2 cell = ivec2(int(cellIdx) % u_sdfNearColumns, int(cellIdx) / u_sdfNearColumns) * kNearTileStride;
		// Stored texel space of this tile: lattice coordinate minus the tile's origin, plus the gutter.
		vec2  t	   = local / kNearTexelM - vec2(tile * kNearTileTexels) + 1.0;
		vec3  v	   = textureLod(u_sdfNear, (vec2(cell) + t) / vec2(textureSize(u_sdfNear, 0)), 0.0).rgb;
		ivec2 k	   = clamp(ivec2(floor(t)), ivec2(1), ivec2(kNearTileTexels));
		s.d		   = v.r;
		s.g		   = v.g;
		s.kind	   = int(texelFetch(u_sdfNear, cell + k, 0).b + 0.5);
	} else {
		ivec2 size = textureSize(u_sdfFar, 0);
		vec2  t	   = local / kFarTexelM + 1.0;
		vec3  v	   = textureLod(u_sdfFar, t / vec2(size), 0.0).rgb;
		s.d		   = v.r;
		s.g		   = v.g;
		s.kind	   = int(texelFetch(u_sdfFar, clamp(ivec2(floor(t)), ivec2(0), size - 1), 0).b + 0.5);
	}
	return s;
}

vec4 sampleShoreProfile(vec2 local) {
	return textureLod(u_shoreProfile, (local / kDetailTexelM + 1.0) / vec2(textureSize(u_shoreProfile, 0)), 0.0);
}

struct ChannelFrame {
	float s;        // arc length along the thalweg, unwrapped across the footprint
	float ratio;    // width / mean width
	float curv;     // signed curvature * half-width
	float coverage; // bilinear weight of valid texels (0 off-channel)
};

// Arc length wraps at kArcWrapM and invalid texels (off-channel) are zero, so
// neither survives hardware bilinear filtering: fetch the four texels, unwrap
// each against a valid one, and blend over the valid ones only.
ChannelFrame sampleChannelFrame(vec2 local) {
	ivec2 size = textureSize(u_channelFrame, 0);
	vec2  c	   = local / kDetailTexelM + 0.5; // texel space (+1 gutter, -0.5 center)
	ivec2 base = ivec2(floor(c));
	vec2  f	   = c - vec2(base);
	vec3  t[4];
	t[0] = texelFetch(u_channelFrame, clamp(base, ivec2(0), size - 1), 0).rgb;
	t[1] = texelFetch(u_channelFrame, clamp(base + ivec2(1, 0), ivec2(0), size - 1), 0).rgb;
	t[2] = texelFetch(u_channelFrame, clamp(base + ivec2(0, 1), ivec2(0), size - 1), 0).rgb;
	t[3] = texelFetch(u_channelFrame, clamp(base + ivec2(1, 1), ivec2(0), size - 1), 0).rgb;
	float w[4] = float[4]((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);

	float ref	= 0.0;
	bool  found = false;
	for (int i = 0; i < 4; ++i) {
		if (!found && t[i].g > 0.0) {
			ref	  = t[i].r;
			found = true;
		}
	}

	ChannelFrame fr = ChannelFrame(0.0, 0.0, 0.0, 0.0);
	for (int i = 0; i < 4; ++i) {
		float wi = t[i].g > 0.0 ? w[i] : 0.0;
		float di = t[i].r - ref;
		fr.s += wi * (ref + di - kArcWrapM * floor(di / kArcWrapM + 0.5));
		fr.ratio += wi * t[i].g;
		fr.curv += wi * t[i].b;
		fr.coverage += wi;
	}
	if (fr.coverage > 1e-4) {
		fr.s /= fr.coverage;
		fr.ratio /= fr.coverage;
		fr.curv /= fr.coverage;
	}
	return fr;
}

// ============================================================================
// SHADING
// ============================================================================

/// A shore band's coverage on the land side: 1 at the waterline, fading over
/// its tail to 0 at w. Zero width removes it. The fade is at least a pixel, so
/// a band narrowing to nothing doesn't alias.
float shoreBand(float d, float w, float aa) {
	return w > 0.0 ? 1.0 - smoothstep(min(w * u_bandTail, w - aa), w, d) : 0.0;
}

vec3 shadeWater(vec3 ground, vec2 local, vec2 world) {
	TerrainSdf sdf = sampleSdf(local);
	float	   aa  = u_metersPerPixel;

	// Past the widest land band and the edge stroke nothing paints.
	float bandReach = max(max(u_drySandW, u_wetSandW), u_mudW * u_wetlandMudGain) * (1.0 + u_alongAmp);
	float reach		= bandReach + u_edgeW * (1.0 + u_edgeSlopeGain) + u_wobbleAmp + aa;
	if (sdf.d >= reach) {
		return ground;
	}

	// Far zoom: a pixel spans more than a band, the fine effects would only alias.
	bool fine = aa <= u_lodBandM;

	// Wobble the distance itself, under a texel, so the line never reads as a vector outline.
	float d = sdf.d;
	if (fine) {
		d += u_wobbleAmp * waterFbm(world / u_wobbleWavelength, 3, kSeedWobble);
	}

	vec4  prof	   = sampleShoreProfile(local);
	float slope	   = prof.r;
	float exposure = prof.g;
	float wSand	   = prof.b;
	float wMud	   = prof.a;
	bool  wetland  = sdf.kind == kWaterKindWetland;
	float isOcean  = sdf.kind == kWaterKindOcean ? 1.0 : 0.0;
	if (wetland) { // no beach: the sediment is mud, in a wider band
		wMud += wSand;
		wSand = 0.0;
	}
	float wGrass  = max(1.0 - wSand - wMud, 0.0);
	float gentle  = 1.0 - slope;
	float along	  = 1.0 + u_alongAmp * waterFbm(world / u_alongWavelength, 2, kSeedAlong);
	float mudGain = wetland ? u_wetlandMudGain : 1.0;

	float water = 1.0 - smoothstep(-0.5 * aa, 0.5 * aa, d);

	// Waterline stroke on both sides: wider and darker on steep shores, almost
	// none on gentle ones. Held to a pixel wide, with its darkness scaled down to match.
	float edgeW	   = u_edgeW * (1.0 + u_edgeSlopeGain * slope);
	float edgeDraw = max(edgeW, aa);
	float edge	   = (1.0 - smoothstep(0.0, edgeDraw, abs(d))) * mix(u_edgeMin, u_edgeMax, slope) * (edgeW / edgeDraw);

	// Land: dry sand, wet sand, mud bank, painted low to high so each cross-fades over its tail.
	vec3 land = ground;
	land	  = mix(land, u_drySand, shoreBand(d, u_drySandW * gentle * wSand * along, aa));
	land	  = mix(land, u_wetSand, shoreBand(d, u_wetSandW * gentle * wSand * along, aa));
	land	  = mix(land, u_mudBank, shoreBand(d, u_mudW * mudGain * gentle * wMud * along, aa));
	land	  = mix(land, u_edgeColor, edge);
	if (water <= 0.0) {
		return land;
	}

	// Depth from distance and slope: wide shallows on a gentle shore, a sliver on a steep one.
	float shallowsW = clamp(u_shallowsK / max(slope, u_slopeMin), u_shallowsMin, u_shallowsMax);
	float t			= clamp(-d / (shallowsW * along), 0.0, 1.0);
	vec3  bed		= wSand * u_drySand + wMud * u_mudBank + wGrass * u_bedGrass;
	vec3  w			= mix(u_waterShallow, u_waterMid, smoothstep(0.0, u_midAt, t));
	w				= mix(w, u_waterDeep, smoothstep(u_midAt, 1.0, t));
	w				= mix(mix(bed, w, u_bedOpacity), w, smoothstep(0.0, u_bedFade, t));

	// Rivers: the deep channel follows the thalweg, in units of the local half-width.
	float channel = 1.0 - smoothstep(u_thalwegInner, u_thalwegOuter, sdf.g);
	w			  = mix(w, u_waterDeep, channel * u_thalwegDepth);

	if (fine) {
		// Riffles on wide straight reaches, pools in narrow bends. The streak
		// wavelength is snapped so the pattern tiles the arc-length wrap.
		if (sdf.g < kNoThalweg) {
			ChannelFrame fr = sampleChannelFrame(local);
			if (fr.coverage > 0.0) {
				float bend	 = smoothstep(0.10, 0.25, abs(fr.curv));
				float riffle = smoothstep(1.1, 1.3, fr.ratio) * (1.0 - bend) * fr.coverage;
				float pool	 = (1.0 - smoothstep(0.8, 0.95, fr.ratio)) * bend * fr.coverage;
				float lambda = kArcWrapM / max(1.0, floor(kArcWrapM / u_riffleWavelength + 0.5));
				float phase	 = (fr.s - u_time * u_flowSpeed) * (kTau / lambda) + waterFbm(world * 0.5, 2, kSeedStreak) * 2.0;
				float streak = 0.5 + 0.5 * sin(phase);
				w			 = mix(w, u_riffleLight, riffle * streak * u_riffleAmp * (1.0 - channel));
				w			 = mix(w, u_waterDeep, pool * u_poolAmp);
			}
		}

		// Sandbars and weed beds, gated by depth so they never touch the shore bands.
		float v		= waterFbm(world / u_patchWavelength, 3, kSeedPatch);
		float depth = smoothstep(0.3, 0.5, t);
		w			= mix(w, u_barColor, smoothstep(u_barT0, u_barT1, v) * depth * u_patchAmp);
		w			= mix(w, u_weedColor, smoothstep(u_weedT0, u_weedT1, -v) * depth * u_patchAmp);
		w += u_shimmerAmp * waterFbm(world * u_shimmerFreq + u_time * u_shimmerDrift * vec2(1.0, 0.6), 2, kSeedShimmer);
	}

	// Foam only on exposed ocean shore, in a thin band on the water side. The
	// 0.2..0.6 gate reads the noise in [0, 1].
	float foamDraw	= max(u_foamW, aa);
	float foamBand	= smoothstep(-foamDraw, 0.0, d) * (u_foamW / foamDraw);
	float foamNoise = 0.5 + 0.5 * waterFbm(world / u_foamWavelength + u_time * u_foamDrift, 2, kSeedFoam);
	float foam = exposure * isOcean * foamBand * smoothstep(0.2, 0.6, foamNoise);
	w = mix(w, u_foam, foam);

	w = mix(w, u_edgeColor, edge);
	return mix(land, w, water);
}
