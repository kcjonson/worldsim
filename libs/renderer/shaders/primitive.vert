#version 330 core

// Input attributes
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_rectLocalPos;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_borderData;
layout(location = 4) in vec4 a_shapeParams;

// Uniforms
uniform mat4 u_projection;
uniform mat4 u_transform;

// Outputs to fragment shader
out vec2 v_rectLocalPos;
out vec4 v_color;
out vec4 v_borderData;
out vec4 v_shapeParams;

void main() {
	// Pass data through to fragment shader
	v_rectLocalPos = a_rectLocalPos;
	v_color = a_color;
	v_borderData = a_borderData;
	v_shapeParams = a_shapeParams;

	// Transform position to clip space
	gl_Position = u_projection * u_transform * vec4(a_position, 0.0, 1.0);
}
