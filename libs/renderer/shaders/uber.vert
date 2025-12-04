#version 330 core

// Uber Shader - Unified vertex shader for shapes, text, and instanced entities
// Combines primitive.vert, msdf_text.vert, and GPU instancing functionality

// Standard vertex attributes (per-vertex data)
layout(location = 0) in vec2 a_position;   // Screen-space position (or local mesh pos for instancing)
layout(location = 1) in vec2 a_texCoord;   // UV for text, rectLocalPos for shapes
layout(location = 2) in vec4 a_color;      // Fill color RGBA
layout(location = 3) in vec4 a_data1;      // borderData for shapes, unused for text
layout(location = 4) in vec4 a_data2;      // shapeParams for shapes, (pixelRange, 0, 0, renderMode) for text
layout(location = 5) in vec4 a_clipBounds; // Clip rect (minX, minY, maxX, maxY) or (0,0,0,0) for no clip

// Include instancing support (attributes 6-7, uniforms, helper functions)
#include "includes/instancing.glsl"

// Standard uniforms (used by non-instanced path)
uniform mat4 u_projection;
uniform mat4 u_transform;

// Outputs to fragment shader
out vec2 v_texCoord;
out vec4 v_color;
out vec4 v_data1;
out vec4 v_data2;
out vec4 v_clipBounds;

// Render mode constant for instanced entities
const float kRenderModeInstanced = -2.0;

void main() {
	vec2 finalPosition;
	vec4 finalColor;

	if (u_instanced != 0) {
		// ========== INSTANCED RENDERING PATH ==========
		// a_position is in local mesh space (centered around origin)
		// Transform using instance data (world position, rotation, scale)
		finalPosition = instanceToScreen(a_position);

		// Apply instance color tint
		finalColor = a_color * getInstanceColorTint();

		// Transform screen-space position to clip space using the same projection as batched path
		gl_Position = u_projection * u_transform * vec4(finalPosition, 0.0, 1.0);

		// Mark as instanced entity rendering (fragment shader will use simple color output)
		v_texCoord = vec2(0.0, 0.0);
		v_data1 = vec4(0.0, 0.0, 0.0, 0.0);
		v_data2 = vec4(0.0, 0.0, 0.0, kRenderModeInstanced);
		v_clipBounds = vec4(0.0, 0.0, 0.0, 0.0);  // No clipping for world entities
	} else {
		// ========== STANDARD BATCHED PATH ==========
		// a_position is already in screen space
		finalPosition = a_position;
		finalColor = a_color;

		// Transform position to clip space
		gl_Position = u_projection * u_transform * vec4(finalPosition, 0.0, 1.0);

		// Pass attribute data through to fragment shader
		v_texCoord = a_texCoord;
		v_data1 = a_data1;
		v_data2 = a_data2;
		v_clipBounds = a_clipBounds;
	}

	// Color is always passed through
	v_color = finalColor;
}
