#version 330 core

in vec2 v_texCoord;
in vec4 v_color;

out vec4 FragColor;

void main() {
	FragColor = v_color;
}
