#version 330 core

layout (location = 0) in vec2 aPosition;  // Vertex position
layout (location = 1) in vec2 aTexCoord;  // Texture coordinate
layout (location = 2) in vec4 aColor;     // Per-vertex color

out vec2 vTexCoord;
out vec4 vColor;

uniform mat4 projection;

void main() {
    gl_Position = projection * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
