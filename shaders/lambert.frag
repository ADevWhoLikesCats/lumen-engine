#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform vec3  uAmbient;
uniform vec3  uBaseColor;
uniform vec3  uCameraPos;
uniform sampler2D uAlbedo;
uniform bool  uUseTexture;
uniform vec2  uUVScale;

uniform mat4      uLightSpace;
uniform sampler2DShadow uShadowMap;
uniform float     uShadowTexel;

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
    vec2 uv = vUV * uUVScale;
    vec3 albedo = uUseTexture ? texture(uAlbedo, uv).rgb : uBaseColor;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    float ndl = max(dot(N, L), 0.0);

    float sh = shadowFactor(vWorldPos);
    vec3 color = albedo * (uAmbient + uLightColor * ndl * sh);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
