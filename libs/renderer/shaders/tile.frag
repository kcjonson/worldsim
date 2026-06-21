#version 330 core

// Tile pass fragment shader - shades ground tiles from a per-chunk tile-data
// texture (one RGBA32UI texel per tile, mirroring engine TileRenderData).
// Replaces the per-tile quad path that rebuilt CPU geometry every frame.

#include "includes/tile.glsl"

in vec2 v_worldPos;

out vec4 FragColor;

// Per-chunk tile data. Texel layout matches TileRenderData (16 bytes, little-endian):
//   r: surfaceId | edgeMask<<8 | cornerMask<<16 | hardEdgeMask<<24
//   g: neighborN | neighborE<<8 | neighborS<<16 | neighborW<<24
//   b: neighborNW | neighborNE<<8 | neighborSE<<16 | neighborSW<<24
//   a: padding
uniform usampler2D u_tileData;
uniform vec2 u_chunkOrigin;      // chunk origin in world meters
uniform ivec2 u_chunkTileOrigin; // chunk origin in world tile coordinates

// Tile atlas. Rects specify uvMin.xy, uvMax.xy per surface id.
uniform sampler2D u_tileAtlas;
uniform int u_tileAtlasRectCount;
// Array size must match kMaxTileAtlasRects in Primitives.cpp (currently 64)
uniform vec4 u_tileAtlasRects[64];

/// Sample the tile atlas for a given surface ID and intra-tile UV.
vec4 sampleTileColor(vec2 uv, uint surfaceId) {
	int idx = int(surfaceId);
	if (idx < u_tileAtlasRectCount) {
		vec4 rect = u_tileAtlasRects[idx];
		vec2 atlasUV = rect.xy + uv * (rect.zw - rect.xy);
		return texture(u_tileAtlas, atlasUV);
	}
	return vec4(1.0);
}

/// Tint water by its rendered depth byte: shallow streams pale, deep rivers and
/// lakes dark. A little procedural shimmer keeps large bodies from reading flat.
vec3 waterColorByDepth(uint depthByte, vec2 worldPos) {
	float d = float(depthByte) / 255.0;
	vec3 shallow = vec3(0.40, 0.62, 0.66); // pale teal
	vec3 deep    = vec3(0.07, 0.24, 0.42); // deep blue
	vec3 c = mix(shallow, deep, d);
	float ripple = tileNoise2D(worldPos * 0.6);
	return c * (0.92 + 0.12 * ripple);
}

void main() {
	// World position -> tile within this chunk. Tiles are 1m, so the intra-tile
	// UV is just the fractional part (y=0 at the tile's north edge).
	vec2 local = v_worldPos - u_chunkOrigin;
	ivec2 tileCoord = clamp(ivec2(floor(local)), ivec2(0), textureSize(u_tileData, 0) - 1);
	vec2 uv = clamp(local - vec2(tileCoord), 0.0, 1.0);

	uvec4 data = texelFetch(u_tileData, tileCoord, 0);
	uint surfaceId    =  data.r         & 0xFFu;
	uint edgeMask     = (data.r >> 8u)  & 0xFFu;
	uint cornerMask   = (data.r >> 16u) & 0xFFu;
	uint hardEdgeMask = (data.r >> 24u) & 0xFFu;
	uint neighborN    =  data.g         & 0xFFu;
	uint neighborE    = (data.g >> 8u)  & 0xFFu;
	uint neighborS    = (data.g >> 16u) & 0xFFu;
	uint neighborW    = (data.g >> 24u) & 0xFFu;
	uint neighborNW   =  data.b         & 0xFFu;
	uint neighborNE   = (data.b >> 8u)  & 0xFFu;
	uint neighborSE   = (data.b >> 16u) & 0xFFu;
	uint neighborSW   = (data.b >> 24u) & 0xFFu;
	uint waterDepth   =  data.a         & 0xFFu;

	// PERF: Early-out for INTERIOR TILES (no edge transitions at all).
	// A tile is truly interior only if ALL 8 neighbors have the same surface.
	bool isInteriorTile = (edgeMask == 0u && cornerMask == 0u && hardEdgeMask == 0u &&
		neighborN == surfaceId && neighborE == surfaceId &&
		neighborS == surfaceId && neighborW == surfaceId &&
		neighborNW == surfaceId && neighborNE == surfaceId &&
		neighborSE == surfaceId && neighborSW == surfaceId);
	if (isInteriorTile) {
		vec4 interiorColor = sampleTileColor(uv, surfaceId);
		if (surfaceId == 4u) {
			interiorColor.rgb = waterColorByDepth(waterDepth, v_worldPos);
		}
		FragColor = interiorColor;
		return;
	}

	int tileX = u_chunkTileOrigin.x + tileCoord.x;
	int tileY = u_chunkTileOrigin.y + tileCoord.y;

	vec4 color = sampleTileColor(uv, surfaceId);
	if (surfaceId == 4u) {
		color.rgb = waterColorByDepth(waterDepth, v_worldPos);
	}

	// PERF: Early-out for interior pixels (~31% of tile area).
	// Blend width is 0.20, corner radius 0.18, edge darkening ~0.12.
	const float kEdgeMargin = 0.22; // Slightly beyond max blend/edge width
	bool isInterior = uv.x > kEdgeMargin && uv.x < (1.0 - kEdgeMargin) &&
	                  uv.y > kEdgeMargin && uv.y < (1.0 - kEdgeMargin);

	if (!isInterior) {
		// ========== SOFT EDGE BLENDING - "Higher Bleeds Onto Lower" ==========
		if (u_tileAtlasRectCount > 0) {
			vec4 blendWeights = computeHigherBleedWeights(uv, tileX, tileY, surfaceId, neighborN, neighborE, neighborS, neighborW, hardEdgeMask);

			#define SAMPLE_NEIGHBOR(neighborId, weight) \
				if (weight > 0.001 && int(neighborId) < u_tileAtlasRectCount) { \
					vec4 nRect = u_tileAtlasRects[int(neighborId)]; \
					vec2 nAtlasUV = nRect.xy + uv * (nRect.zw - nRect.xy); \
					vec4 nColor = texture(u_tileAtlas, nAtlasUV); \
					color = mix(color, nColor, weight); \
				}

			SAMPLE_NEIGHBOR(neighborN, blendWeights.x)
			SAMPLE_NEIGHBOR(neighborE, blendWeights.y)
			SAMPLE_NEIGHBOR(neighborS, blendWeights.z)
			SAMPLE_NEIGHBOR(neighborW, blendWeights.w)

			// Diagonal corner blending
			vec4 diagWeights = computeDiagonalCornerWeights(uv, surfaceId,
				neighborN, neighborE, neighborS, neighborW,
				neighborNW, neighborNE, neighborSE, neighborSW);

			SAMPLE_NEIGHBOR(neighborNW, diagWeights.x)
			SAMPLE_NEIGHBOR(neighborNE, diagWeights.y)
			SAMPLE_NEIGHBOR(neighborSE, diagWeights.z)
			SAMPLE_NEIGHBOR(neighborSW, diagWeights.w)

			#undef SAMPLE_NEIGHBOR
		}

		// Apply procedural edge/corner darkening
		float darkenFactor = computeTileEdgeDarkening(uv, tileX, tileY, edgeMask, cornerMask, hardEdgeMask);
		color.rgb *= darkenFactor;
	}

	FragColor = color;
}
