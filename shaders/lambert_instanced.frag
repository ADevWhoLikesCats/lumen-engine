#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;
in float vMaterial;

uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform vec3  uAmbient;
uniform vec3  uCameraPos;
uniform sampler2D uAlbedo;
uniform sampler2D uCrateTex;
uniform bool  uUseTexture;
uniform vec2  uUVScale;

uniform mat4      uLightSpace;
uniform sampler2DShadow uShadowMap;
uniform float     uShadowTexel;

uniform vec3  uFogHorizon;
uniform vec3  uFogZenith;
uniform float uFogDensity;

out vec4 FragColor;

float shadowFactor(vec3 worldPos) {
    vec4 lsPos = uLightSpace * vec4(worldPos, 1.0);
    vec3 proj  = lsPos.xyz / lsPos.w * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;
    float bias = 0.0015;
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            vec2 off = vec2(x, y) * uShadowTexel;
            s += texture(uShadowMap, vec3(proj.xy + off, proj.z - bias));
        }
    return s / 9.0;
}

void main() {
    vec3 albedo = (vMaterial > 0.5) ? texture(uCrateTex, vUV).rgb : vColor;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    float ndl = max(dot(N, L), 0.0);

    float sh = shadowFactor(vWorldPos);
    vec3 color = albedo * (uAmbient + uLightColor * ndl * sh);

    vec3  viewDir = normalize(vWorldPos - uCameraPos);
    float t       = pow(max(viewDir.y, 0.0), 0.55);
    vec3  fogCol  = mix(uFogHorizon, uFogZenith, t);
    float dist    = length(vWorldPos - uCameraPos);
    float fogF    = 1.0 - exp(-dist * uFogDensity);
    color = mix(color, fogCol, fogF);

    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
