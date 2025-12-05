#version 330 core

// Uber Shader - Unified fragment shader for shapes and text
// Combines primitive.frag (SDF shapes) and msdf_text.frag (MSDF text)

in vec2 v_texCoord;
in vec4 v_color;
in vec4 v_data1;
in vec4 v_data2;
in vec4 v_clipBounds;  // Clip rect (minX, minY, maxX, maxY) or (0,0,0,0) for no clip

out vec4 FragColor;

// MSDF font atlas texture (bound once per frame, ignored for shapes)
uniform sampler2D u_atlas;

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

// ============================================================================
// MAIN - Branch on render mode
// ============================================================================

void main() {
	// Render mode detection:
	// - Shapes:    v_data2.w >= 0 (borderPosition: 0=Inside, 1=Center, 2=Outside)
	// - Text:      v_data2.w == -1.0
	// - Instanced: v_data2.w == -2.0

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
