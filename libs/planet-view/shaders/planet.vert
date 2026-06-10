#version 330 core

layout(location = 0) in vec3 a_position;  // unit-sphere position
layout(location = 1) in vec2 a_uv;        // rhombus-local UV [0,1]

uniform mat4 u_mvp;
uniform mat4 u_model;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;

void main() {
    v_normal   = normalize(mat3(u_model) * a_position);
    v_worldPos = vec3(u_model * vec4(a_position, 1.0));
    v_uv       = a_uv;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
