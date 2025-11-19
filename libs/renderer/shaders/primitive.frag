#version 330 core

// Inputs from vertex shader
in vec2 v_rectLocalPos;
in vec4 v_color;
in vec4 v_borderData;
in vec4 v_shapeParams;

// Output
out vec4 FragColor;

// SDF for rounded rectangle
// Based on Inigo Quilez's optimized implementation
// https://iquilezles.org/articles/distfunctions2d/
float sdRoundedBox(vec2 p, vec2 size, float radius) {
	// Clamp radius to half the smallest dimension to prevent invalid shapes
	radius = min(radius, min(size.x, size.y));

	// Exploit symmetry - work in positive quadrant
	vec2 q = abs(p) - size + radius;

	// Distance calculation
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
	// Unpack shape parameters
	vec2 halfSize = v_shapeParams.xy;
	float cornerRadius = v_shapeParams.z;
	int borderPos = int(v_shapeParams.w + 0.5);  // Round to nearest int

	// Unpack border data
	vec3 borderColor = v_borderData.rgb;
	float borderWidth = v_borderData.a;

	// Calculate SDF distance to rect edge
	float dist = sdRoundedBox(v_rectLocalPos, halfSize, cornerRadius);

	// Anti-aliasing: smooth transition over ~1 pixel in screen space
	// Use screen-space derivatives to determine pixel size
	float pixelSize = length(vec2(dFdx(dist), dFdy(dist)));

	// Calculate alpha for shape boundary (1.0 inside, 0.0 outside)
	float shapeAlpha = 1.0 - smoothstep(-pixelSize, pixelSize, dist);

	// Early exit if completely transparent (optimization)
	if (shapeAlpha < 0.001) {
		discard;
	}

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

	// Apply shape alpha and fill alpha
	FragColor = vec4(finalColor, shapeAlpha * v_color.a);
}
