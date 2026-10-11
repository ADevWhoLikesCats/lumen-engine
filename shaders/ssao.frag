#version 330 core
uniform sampler2D uSceneDepth;
uniform mat4 uInvViewProj;
uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform vec2 uResolution;
uniform float uRadius;
uniform float uBias;
uniform float uIntensity;

out vec4 fragColor;

vec3 world_from_depth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * ndc;
    return world.xyz / world.w;
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    float depth = texture(uSceneDepth, uv).r;

    if (depth >= 0.9999) {
        fragColor = vec4(1.0);
        return;
    }

    vec3 world = world_from_depth(uv, depth);

    // Reconstruct normal from depth derivatives
    vec2 texel = 1.0 / uResolution;
    vec3 p_right = world_from_depth(uv + vec2(texel.x, 0.0),
                                    texture(uSceneDepth, uv + vec2(texel.x, 0.0)).r);
    vec3 p_up    = world_from_depth(uv + vec2(0.0, texel.y),
                                    texture(uSceneDepth, uv + vec2(0.0, texel.y)).r);
    vec3 N = normalize(cross(p_right - world, p_up - world));

    // TBN around N with random rotation
    float ang = hash(gl_FragCoord.xy) * 6.2831853;
    vec3 random_vec = vec3(cos(ang), sin(ang), 0.0);
    vec3 T = normalize(random_vec - N * dot(random_vec, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    const int SAMPLES = 12;
    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        float fi = float(i);
        float phi = fi * 2.39996323;
        float cosTheta = sqrt(1.0 - fi / float(SAMPLES));
        float sinTheta = sqrt(fi / float(SAMPLES));
        vec3 dir_ts = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

        float scale = mix(0.1, 1.0, fi / float(SAMPLES));
        vec3 sample_world = world + TBN * dir_ts * uRadius * scale;

        vec4 clip = uViewProj * vec4(sample_world, 1.0);
        if (clip.w <= 0.0) continue;
        vec3 ndc = clip.xyz / clip.w;
        vec2 sample_uv = ndc.xy * 0.5 + 0.5;

        if (sample_uv.x < 0.0 || sample_uv.x > 1.0 ||
            sample_uv.y < 0.0 || sample_uv.y > 1.0) continue;

        float sample_depth = texture(uSceneDepth, sample_uv).r;
        vec3 scene_world = world_from_depth(sample_uv, sample_depth);

        vec3 diff = scene_world - sample_world;
        float dist = length(diff);
        if (dot(N, diff) > 0.0) {
            occlusion += 1.0 / (1.0 + dist * dist);
        }
    }

    float ao = 1.0 - clamp(occlusion / float(SAMPLES) * uIntensity, 0.0, 1.0);
    fragColor = vec4(ao, ao, ao, 1.0);
}
