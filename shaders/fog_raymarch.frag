#version 330 core
uniform sampler2D uSceneDepth;
uniform sampler2DArray uShadowMap;
uniform mat4      uInvViewProj;
uniform mat4      uLightSpace[3];
uniform float     uCascadeSplits[3];
uniform float     uShadowTexel;
uniform vec3      uCamPos;
uniform vec3      uLightDir;
uniform vec3      uLightColor;
uniform float     uLightIntensity;
uniform vec2      uFullResolution;
uniform vec2      uHalfResolution;
uniform float     uFogDensity;
uniform float     uFogHeightFall;
uniform float     uFogMaxDist;
uniform float     uFogSteps;
uniform float     uSunScatter;
uniform float     uAmbientScatter;

out vec4 fragColor;

vec3 world_from_depth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * ndc;
    return world.xyz / world.w;
}

int pickCascade(float viewDepth) {
    if (viewDepth < uCascadeSplits[0]) return 0;
    if (viewDepth < uCascadeSplits[1]) return 1;
    return 2;
}

// Single-sample shadow lookup for the fog — no PCF, we're already
// averaging over many steps. A miss is a lot cheaper than a block.
float shadow_at(vec3 world_pos) {
    // Estimate view depth for cascade selection: distance along the
    // light-camera axis is close enough to the view-camera axis for
    // most scenes.
    vec3 toPos = world_pos - uCamPos;
    float viewDepth = length(toPos);

    int cascade = pickCascade(viewDepth);
    vec4 lsPos = uLightSpace[cascade] * vec4(world_pos, 1.0);
    vec3 proj = lsPos.xyz / lsPos.w * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }
    float d = texture(uShadowMap, vec3(proj.xy, float(cascade))).r;
    float bias = 0.0015;
    return (proj.z - bias <= d) ? 1.0 : 0.0;
}

// Henyey-Greenstein phase function — forward scattering toward the sun
float hg_phase(float cos_theta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(denom, 1.5));
}

void main() {
    vec2 half_uv = gl_FragCoord.xy / uHalfResolution;
    float depth = texture(uSceneDepth, half_uv).r;

    // Reconstruct ray direction and end distance
    vec4 ndcNear = vec4(half_uv * 2.0 - 1.0, -1.0, 1.0);
    vec4 ndcFar  = vec4(half_uv * 2.0 - 1.0,  1.0, 1.0);
    vec4 worldNear = uInvViewProj * ndcNear;
    vec4 worldFar  = uInvViewProj * ndcFar;
    vec3 nearPos = worldNear.xyz / worldNear.w;
    vec3 farPos  = worldFar.xyz  / worldFar.w;
    vec3 ray_dir = normalize(farPos - nearPos);

    float scene_dist;
    if (depth >= 0.9999) {
        scene_dist = uFogMaxDist;
    } else {
        vec3 world = world_from_depth(half_uv, depth);
        scene_dist = length(world - uCamPos);
    }
    scene_dist = min(scene_dist, uFogMaxDist);

    vec3 L = normalize(-uLightDir);
    float cos_theta = dot(ray_dir, L);
    float phase_sun     = hg_phase(cos_theta, 0.6);
    float phase_ambient = 1.0 / (4.0 * 3.14159265);

    float step_size = scene_dist / uFogSteps;

    vec3 accum_scatter = vec3(0.0);
    float accum_transmit = 1.0;

    // Jitter the start to break banding
    float jitter = fract(52.9829189 * fract(0.06711056 * gl_FragCoord.x +
                                            0.00583715 * gl_FragCoord.y));

    vec3 light_radiance = uLightColor * uLightIntensity;

    for (int i = 0; i < 128; ++i) {
        if (float(i) >= uFogSteps) break;

        float t = (float(i) + jitter) * step_size;
        if (t > scene_dist) break;

        vec3 pos = uCamPos + ray_dir * t;

        float heightFactor = exp(-uFogHeightFall * max(pos.y, 0.0));
        float density = uFogDensity * heightFactor;

        float shadow = shadow_at(pos);

        vec3 scatter_sun     = light_radiance * phase_sun     * uSunScatter     * shadow;
        vec3 scatter_ambient = light_radiance * phase_ambient * uAmbientScatter;

        vec3 scatter = (scatter_sun + scatter_ambient) * density * step_size;

        float extinction = density * step_size;
        accum_scatter += accum_transmit * scatter;
        accum_transmit *= exp(-extinction);
    }

    fragColor = vec4(accum_scatter, accum_transmit);
}
