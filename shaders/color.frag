// Applies animated over time gradient to the user texture.
#version 330

precision mediump float;

uniform vec2 resolution;
uniform int marker_radius;

in vec4 color;
in vec2 seed;
out vec4 out_color;

#define SEED_MARKER_COLOR vec4(.1, .1, .1, 1)

void main(void) {
    if (length(gl_FragCoord.xy - seed) < marker_radius) {
        gl_FragDepth = 0;
        out_color = SEED_MARKER_COLOR;
    } else {
        gl_FragDepth = length(gl_FragCoord.xy - seed)/length(resolution);
        out_color = color;
    }
}
