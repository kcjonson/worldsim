#version 330 core

// Uber Shader - Unified vertex shader for shapes and text
// Combines primitive.vert and msdf_text.vert functionality

// Input attributes (unified vertex format)
layout(location = 0) in vec2 a_position;   // Screen-space position
layout(location = 1) in vec2 a_texCoord;   // UV for text, rectLocalPos for shapes
layout(location = 2) in vec4 a_color;      // Fill color RGBA
layout(location = 3) in vec4 a_data1;      // borderData for shapes, unused for text
layout(location = 4) in vec4 a_data2;      // shapeParams for shapes, (pixelRange, 0, 0, renderMode) for text
layout(location = 5) in vec4 a_clipBounds; // Clip rect (minX, minY, maxX, maxY) or (0,0,0,0) for no clip

// Uniforms
uniform mat4 u_projection;
uniform mat4 u_transform;

// Outputs to fragment shader
out vec2 v_texCoord;
out vec4 v_color;
out vec4 v_data1;
out vec4 v_data2;
out vec4 v_clipBounds;

void main() {
	// Pass data through to fragment shader
	v_texCoord = a_texCoord;
	v_color = a_color;
	v_data1 = a_data1;
	v_data2 = a_data2;
	v_clipBounds = a_clipBounds;

	// Transform position to clip space
	gl_Position = u_projection * u_transform * vec4(a_position, 0.0, 1.0);
}
