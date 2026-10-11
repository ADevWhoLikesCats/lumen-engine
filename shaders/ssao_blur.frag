#version 330 core
uniform sampler2D uSSAO;
uniform vec2 uInvResolution;
out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy * uInvResolution;
    vec2 texel = uInvResolution;

    float sum = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            sum += texture(uSSAO, uv + vec2(float(dx), float(dy)) * texel).r;
        }
    }
    float ao = sum / 9.0;
    fragColor = vec4(ao, ao, ao, 1.0);
}
