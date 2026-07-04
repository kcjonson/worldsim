#version 330 core

in vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_alpha;

out vec4 fragColor;

void main() {
    vec4 c = texture(u_tex, v_uv);
    fragColor = vec4(c.rgb, c.a * u_alpha);
}
