#version 330 core
uniform sampler2D uSceneColor;   // gbuf.color (lit scene, this frame)
uniform sampler2D uSceneDepth;   // sceneFB.depth
uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec2 uResolution;

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
        fragColor = vec4(0.0);
        return;
    }

    vec3 world_pos = world_from_depth(uv, depth);

    vec2 texel = 1.0 / uResolution;
    vec3 p_right = world_from_depth(uv + vec2(texel.x, 0.0),
                                    texture(uSceneDepth, uv + vec2(texel.x, 0.0)).r);
    vec3 p_up    = world_from_depth(uv + vec2(0.0, texel.y),
                                    texture(uSceneDepth, uv + vec2(0.0, texel.y)).r);
    vec3 N = normalize(cross(p_right - world_pos, p_up - world_pos));

    vec3 V = normalize(uCamPos - world_pos);
    vec3 R = reflect(-V, N);

    float jitter = hash(gl_FragCoord.xy);
    float step_size = 0.15;
    float thickness = 0.08;

    vec3 hit_color = vec3(0.0);
    float hit_found = 0.0;

    for (int i = 0; i < 60; ++i) {
        float dist = (float(i) + jitter) * step_size;
        vec3 p = world_pos + R * dist;

        vec4 clip = uViewProj * vec4(p, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        vec2 sample_uv = ndc.xy * 0.5 + 0.5;

        if (sample_uv.x < 0.0 || sample_uv.x > 1.0 ||
            sample_uv.y < 0.0 || sample_uv.y > 1.0) break;

        float scene_depth = texture(uSceneDepth, sample_uv).r;
        if (scene_depth >= 0.9999) continue;

        vec3 scene_world = world_from_depth(sample_uv, scene_depth);
        float diff = length(p - scene_world);

        if (diff < thickness && dot(p - scene_world, R) > 0.0) {
            hit_color = texture(uSceneColor, sample_uv).rgb;
            hit_found = 1.0;
            break;
        }
    }

    fragColor = vec4(hit_color, hit_found);
}
