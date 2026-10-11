#version 330 core
uniform sampler2D uCurrent;     // this frame's lit color (gbuf att 0)
uniform sampler2D uHistory;     // last frame's TAA output
uniform sampler2D uGBuffer;     // gbuf att 1, prevUV.x in .a
uniform sampler2D uGBuffer2;    // gbuf att 3, prevUV.y in .a
uniform vec2  uResolution;
uniform float uBlendFactor;     // 0.9 typical

out vec4 fragColor;

vec3 clip_to_neighborhood(vec3 history, vec2 uv, vec2 texel) {
    // 5-tap cross instead of 3x3 — cuts texture reads nearly in half with
    // almost no quality difference for TAA clamping.
    vec3 c0 = texture(uCurrent, uv).rgb;
    vec3 c1 = texture(uCurrent, uv + vec2( 1, 0) * texel).rgb;
    vec3 c2 = texture(uCurrent, uv + vec2(-1, 0) * texel).rgb;
    vec3 c3 = texture(uCurrent, uv + vec2( 0, 1) * texel).rgb;
    vec3 c4 = texture(uCurrent, uv + vec2( 0,-1) * texel).rgb;

    vec3 cmin = min(min(c0, min(c1, c2)), min(c3, c4));
    vec3 cmax = max(max(c0, max(c1, c2)), max(c3, c4));

    vec3 range = (cmax - cmin) * 1.5;
    vec3 mid = (cmax + cmin) * 0.5;
    return clamp(history, mid - range * 0.5, mid + range * 0.5);
}

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    vec2 texel = 1.0 / uResolution;

    vec3 current = texture(uCurrent, uv).rgb;

    // Reconstruct prevUV from packed G-buffer alphas
    float prevU = texture(uGBuffer,  uv).a;
    float prevV = texture(uGBuffer2, uv).a;
    vec2 prevUV = vec2(prevU, prevV);

    // Out-of-bounds history → use current frame
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        fragColor = vec4(current, 1.0);
        return;
    }

    vec3 history = texture(uHistory, prevUV).rgb;
    history = clip_to_neighborhood(history, uv, texel);

    // Velocity-adaptive blend
    vec2 vel = abs(uv - prevUV) * uResolution;
    float vmag = length(vel);
    float blend = uBlendFactor;
    if (vmag > 8.0) blend = max(0.0, blend - (vmag - 8.0) * 0.05);

    vec3 result = mix(current, history, blend);
    fragColor = vec4(result, 1.0);
}
