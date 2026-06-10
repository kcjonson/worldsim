#version 330 core

// Full-screen triangle (no VBO needed — generate positions from vertex ID)
out vec2 v_uv;

void main() {
    vec2 pos = vec2(
        float((gl_VertexID & 1) << 2) - 1.0,
        float((gl_VertexID & 2) << 1) - 1.0
    );
    v_uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
