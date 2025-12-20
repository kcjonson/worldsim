#version 330 core

// Uber Shader - Unified fragment shader for shapes and text
// Combines primitive.frag (SDF shapes) and msdf_text.frag (MSDF text)

#include "includes/tile.glsl"

in vec2 v_texCoord;
in vec4 v_color;
in vec4 v_data1;
in vec4 v_data2;
in vec4 v_clipBounds;  // Clip rect (minX, minY, maxX, maxY) or (0,0,0,0) for no clip
in vec4 v_data3;       // Diagonal neighbors for tiles (NW, NE, SE, SW)

out vec4 FragColor;

// MSDF font atlas texture (bound once per frame, ignored for shapes)
uniform sampler2D u_atlas;
// Tile atlas (optional). Rects specify uvMin.xy, uvMax.xy per surface id.
uniform sampler2D u_tileAtlas;
uniform int u_tileAtlasRectCount;
// Array size must match kMaxTileAtlasRects in BatchRenderer.cpp (currently 64)
uniform vec4 u_tileAtlasRects[64];
uniform int u_softBlendMode; // 0 = off, 1 = placeholder (future)

// Viewport height for Y-coordinate flip (OpenGL origin is bottom-left, UI is top-left)
// NOTE: This is the PHYSICAL framebuffer height in physical pixels
uniform float u_viewportHeight;
// Pixel ratio for DPI scaling (physical pixels / logical pixels)
uniform float u_pixelRatio;

// ============================================================================
// SHAPE SDF FUNCTIONS (from primitive.frag)
// ============================================================================

// SDF for rounded rectangle - Inigo Quilez's optimized implementation
// https://iquilezles.org/articles/distfunctions2d/
float sdRoundedBox(vec2 p, vec2 size, float radius) {
	// Clamp radius to half the smallest dimension to prevent invalid shapes
	radius = min(radius, min(size.x, size.y));

	// Exploit symmetry - work in positive quadrant
	vec2 q = abs(p) - size + radius;

	// Distance calculation
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// ============================================================================
// TEXT MSDF FUNCTIONS (from msdf_text.frag)
// ============================================================================

// Median of three values (for multi-channel SDF)
float median(float r, float g, float b) {
	return max(min(r, g), min(max(r, g), b));
}

// Screen-space partial derivatives to calculate distance field scale
float screenPxRange(float pixelRange) {
	vec2 unitRange = vec2(pixelRange) / vec2(textureSize(u_atlas, 0));
	vec2 screenTexSize = vec2(1.0) / fwidth(v_texCoord);
	return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

// ============================================================================
// RENDER MODE CONSTANTS
// ============================================================================
const float kRenderModeText = -1.0;      // MSDF text rendering
const float kRenderModeInstanced = -2.0; // Simple solid color (instanced entities)
// Tile rendering mode constant. Must match BatchRenderer.cpp kRenderModeTile and uber.vert:31.
const float kRenderModeTile = -3.0;

// ============================================================================
// MAIN - Branch on render mode
// ============================================================================

void main() {
	// Render mode detection:
	// - Shapes:    v_data2.w >= 0 (borderPosition: 0=Inside, 1=Center, 2=Outside)
	// - Text:      v_data2.w == -1.0
	// - Instanced: v_data2.w == -2.0
	// - Tiles:     v_data2.w == -3.0

	// ========== TILE RENDERING (adjacency mask driven) ==========
	// Uses procedural edge variation from includes/tile.glsl
	if (v_data2.w < -2.5) {
		// Unpack mask data (packed as integers in data1.xyzw)
		uint edgeMask = uint(v_data1.x + 0.5);
		uint cornerMask = uint(v_data1.y + 0.5);
		uint surfaceId = uint(v_data1.z + 0.5);
		uint hardEdgeMask = uint(v_data1.w + 0.5);

		// Unpack cardinal neighbor surface IDs from v_clipBounds (repurposed for tiles)
		// Tiles don't use per-vertex clipping, so this slot carries neighbor data
		uint neighborN = uint(v_clipBounds.x + 0.5);
		uint neighborE = uint(v_clipBounds.y + 0.5);
		uint neighborS = uint(v_clipBounds.z + 0.5);
		uint neighborW = uint(v_clipBounds.w + 0.5);

		// Unpack diagonal neighbor surface IDs from v_data3
		uint neighborNW = uint(v_data3.x + 0.5);
		uint neighborNE = uint(v_data3.y + 0.5);
		uint neighborSE = uint(v_data3.z + 0.5);
		uint neighborSW = uint(v_data3.w + 0.5);

		// PERF: Early-out for INTERIOR TILES (no edge transitions at all)
		// A tile is truly interior only if ALL 8 neighbors have the same surface.
		// This skips ALL expensive work: blend weights, neighbor sampling, edge darkening.
		bool isInteriorTile = (edgeMask == 0u && cornerMask == 0u && hardEdgeMask == 0u &&
			neighborN == surfaceId && neighborE == surfaceId &&
			neighborS == surfaceId && neighborW == surfaceId &&
			neighborNW == surfaceId && neighborNE == surfaceId &&
			neighborSE == surfaceId && neighborSW == surfaceId);
		if (isInteriorTile) {
			vec4 color = v_color;
			if (u_tileAtlasRectCount > 0) {
				int idx = int(surfaceId);
				if (idx < u_tileAtlasRectCount) {
					vec2 halfSize = max(v_data2.xy, vec2(0.0001));
					vec2 uv = (v_texCoord / halfSize) * 0.5 + 0.5;
					vec4 rect = u_tileAtlasRects[idx];
					vec2 atlasUV = rect.xy + uv * (rect.zw - rect.xy);
					color = texture(u_tileAtlas, atlasUV) * v_color;
				}
			}
			FragColor = color;
			return;
		}

		// Unpack tile world coordinates from data2.z
		// Packed as: (tileX + 32768) | ((tileY + 32768) << 16)
		uint packedCoord = uint(v_data2.z);
		int tileX = int(packedCoord & 0xFFFFu) - 32768;
		int tileY = int(packedCoord >> 16u) - 32768;

		// Rect-local coordinates map -halfSize..+halfSize → 0..1
		vec2 halfSize = max(v_data2.xy, vec2(0.0001));
		vec2 uv = (v_texCoord / halfSize) * 0.5 + 0.5; // uv.y=0 top

		// Base color from atlas if available, otherwise vertex color
		vec4 color = v_color;
		if (u_tileAtlasRectCount > 0) {
			int idx = int(surfaceId);
			if (idx < u_tileAtlasRectCount) {
				vec4 rect = u_tileAtlasRects[idx];
				vec2 atlasUV = rect.xy + uv * (rect.zw - rect.xy);
				color = texture(u_tileAtlas, atlasUV) * v_color;
			}
		}

		// PERF: Early-out for interior pixels (~31% of tile area)
		// Blend width is 0.20, corner radius 0.18, edge darkening ~0.12
		// Interior pixels need no blending or edge processing
		const float kEdgeMargin = 0.22;  // Slightly beyond max blend/edge width
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
						vec4 nColor = texture(u_tileAtlas, nAtlasUV) * v_color; \
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
		return;
	}

	// ========== INSTANCED ENTITY RENDERING (simple solid color) ==========
	if (v_data2.w < -1.5) {
		// Output the vertex color (includes instance color tint from SVG asset)
		FragColor = v_color;
		return;
	}

	// ========== FAST-PATH RECT CLIPPING (for UI elements only) ==========
	// Clip bounds check: (minX, minY, maxX, maxY) in logical UI coordinates (top-left origin)
	// NOT used for instanced world entities (v_clipBounds is zeroed for those)
	// A clipBounds of (0,0,0,0) means no clipping (maxX <= minX check detects this)
	if (v_clipBounds.z > v_clipBounds.x) {
		// Scale clip bounds from logical pixels to physical pixels for gl_FragCoord comparison
		vec4 physicalClipBounds = v_clipBounds * u_pixelRatio;

		// Convert gl_FragCoord.y from OpenGL coordinates (bottom-left origin, physical pixels)
		// to UI coordinates (top-left origin, physical pixels)
		float physicalY = u_viewportHeight - gl_FragCoord.y;

		// Discard if outside clip bounds (all comparisons in physical pixels)
		if (gl_FragCoord.x < physicalClipBounds.x || gl_FragCoord.x > physicalClipBounds.z ||
			physicalY < physicalClipBounds.y || physicalY > physicalClipBounds.w) {
			discard;
		}
	}

	if (v_data2.w >= 0.0) {
		// ========== SHAPE RENDERING (SDF) ==========
		// Data unpacking:
		// v_texCoord = rectLocalPos (SDF coordinates from rect center)
		// v_data1 = (borderColor.rgb, borderWidth)
		// v_data2 = (halfWidth, halfHeight, cornerRadius, borderPosition)

		vec2 rectLocalPos = v_texCoord;
		vec2 halfSize = v_data2.xy;
		float cornerRadius = v_data2.z;
		int borderPos = int(v_data2.w + 0.5);  // Round to nearest int

		vec3 borderColor = v_data1.rgb;
		float borderWidth = v_data1.a;

		// Calculate SDF distance to rect edge
		float dist = sdRoundedBox(rectLocalPos, halfSize, cornerRadius);

		// Anti-aliasing: smooth transition over ~1 pixel in screen space
		// Use screen-space derivatives to determine pixel size
		float pixelSize = length(vec2(dFdx(dist), dFdy(dist)));

		// Determine border region boundaries based on position mode
		float borderInner, borderOuter;
		if (borderPos == 0) {
			// Inside: border is drawn inside the shape
			borderOuter = 0.0;
			borderInner = -borderWidth;
		} else if (borderPos == 1) {
			// Center: border straddles the shape edge
			borderOuter = borderWidth * 0.5;
			borderInner = -borderWidth * 0.5;
		} else {
			// Outside: border is drawn outside the shape
			borderOuter = borderWidth;
			borderInner = 0.0;
		}

		// Calculate the effective outer edge (shape edge or border outer edge)
		float effectiveOuter = max(0.0, borderOuter);

		// Calculate alpha for the entire renderable area (shape + any outside border)
		float shapeAlpha = 1.0 - smoothstep(-pixelSize + effectiveOuter, pixelSize + effectiveOuter, dist);

		// Early exit if completely transparent (optimization)
		if (shapeAlpha < 0.001) {
			discard;
		}

		// Calculate border blend factor (1.0 in border region, 0.0 in fill)
		float borderBlend = 0.0;
		if (borderWidth > 0.001) {
			// Smooth transition at inner border edge
			float innerFade = smoothstep(borderInner - pixelSize, borderInner + pixelSize, dist);
			// Smooth transition at outer border edge
			float outerFade = 1.0 - smoothstep(borderOuter - pixelSize, borderOuter + pixelSize, dist);
			// Combine: 1.0 only in the region between inner and outer
			borderBlend = innerFade * outerFade;
		}

		// Mix fill color and border color based on blend factor
		vec3 finalColor = mix(v_color.rgb, borderColor, borderBlend);

		// Calculate final alpha: blend between fill alpha and full opacity based on border region
		// In border region (borderBlend=1): use full opacity for border visibility
		// In fill region (borderBlend=0): use fill color's alpha
		float finalAlpha = mix(v_color.a, 1.0, borderBlend);

		// Apply shape boundary alpha
		FragColor = vec4(finalColor, shapeAlpha * finalAlpha);

	} else {
		// ========== TEXT RENDERING (MSDF) ==========
		// Data unpacking:
		// v_texCoord = UV coordinates into MSDF atlas
		// v_color = text color RGBA
		// v_data1 = unused (0,0,0,0)
		// v_data2 = (pixelRange, 0, 0, -1.0)

		float pixelRange = v_data2.x;  // 4.0 from atlas generation

		// Sample the multi-channel distance field
		vec3 msd = texture(u_atlas, v_texCoord).rgb;

		// Calculate signed distance using median-of-three
		float sd = median(msd.r, msd.g, msd.b);

		// Convert to screen-space distance
		float screenPxDistance = screenPxRange(pixelRange) * (sd - 0.5);

		// Anti-aliased opacity using smoothstep for proper anti-aliasing
		float opacity = smoothstep(-0.5, 0.5, screenPxDistance);

		// Output color with opacity
		FragColor = vec4(v_color.rgb, v_color.a * opacity);
	}
}
