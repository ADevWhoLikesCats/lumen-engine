#version 330 core
uniform vec3  uProbePos;
uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform float uLightIntensity;
uniform vec2  uInvResolution;
uniform float uGroundY;
uniform vec3  uGroundAlbedo;
uniform vec3  uSkyHorizon;
uniform vec3  uSkyZenith;

out vec4 fragColor;

const float PI = 3.14159265359;

vec3 oct_decode(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec3 n = vec3(uv.x, uv.y, 1.0 - abs(uv.x) - abs(uv.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

// Trace against a single ground plane at y = uGroundY.
// Anything that doesn't hit the plane is sky.
vec3 trace(vec3 ro, vec3 rd, out bool hitGround) {
    hitGround = false;
    vec3 radiance;

    if (abs(rd.y) > 1e-5) {
        float t = (uGroundY - ro.y) / rd.y;
        if (t > 0.0) {
            hitGround = true;
            vec3 hitPos = ro + rd * t;
            vec3 N = vec3(0.0, 1.0, 0.0);
            vec3 L = normalize(-uLightDir);
            float NdotL = max(dot(N, L), 0.0);
            vec3 diffuse = uGroundAlbedo * uLightColor * uLightIntensity * NdotL;
            vec3 ambient = uGroundAlbedo * 0.4;
            radiance = diffuse + ambient;
            return radiance;
        }
    }

    // Sky gradient
    float up = clamp(rd.y, 0.0, 1.0);
    radiance = mix(uSkyHorizon, uSkyZenith, pow(up, 0.55)) * 1.5;

    // NOTE: no sun disk in the SH capture. The SH probe represents ambient
    // only — the directional light is evaluated separately in the main
    // shader. Adding the sun here double-counts the light and biases the
    // coefficients toward the light color.

    return radiance;
}

void main() {
    vec2 uv = gl_FragCoord.xy * uInvResolution;
    vec3 rd = oct_decode(uv);

    bool hitGround;
    vec3 radiance = trace(uProbePos, rd, hitGround);
    fragColor = vec4(radiance, 1.0);
}
