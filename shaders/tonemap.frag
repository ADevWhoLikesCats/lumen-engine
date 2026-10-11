#version 330 core
uniform sampler2D uScene;
uniform vec2 uInvSize;
out vec4 fragColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x*(a*x + b)) / (x*(c*x + d) + e), 0.0, 1.0);
}
vec3 to_srgb(vec3 c) {
    return mix(pow(c, vec3(1.0/2.4)) * 1.055 - 0.055,
               c * 12.92,
               vec3(lessThanEqual(c, vec3(0.0031308))));
}

void main() {
    vec2 uv = gl_FragCoord.xy * uInvSize;
    vec3 hdr = texture(uScene, uv).rgb;
    vec3 ldr = aces(hdr);
    ldr = to_srgb(ldr);
    fragColor = vec4(ldr, 1.0);
}
