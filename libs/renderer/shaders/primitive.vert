#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
layout(location = 2) in vec4 a_color;

uniform mat4 u_projection;
uniform mat4 u_transform;

out vec2 v_texCoord;
out vec4 v_color;

void main() {
	v_texCoord = a_texCoord;
	v_color = a_color;
	gl_Position = u_projection * u_transform * vec4(a_position, 0.0, 1.0);
}
