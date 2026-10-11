#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;
in float vMaterial;
in vec4 vLightSpacePos;
in float vViewDepth;

uniform vec3  uCamPos;
uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform float uLightIntensity;

uniform sampler2DArray uShadowMap;
uniform mat4  uLightSpace[3];
uniform float uCascadeSplits[3];
uniform float uShadowTexel;
uniform int   uShadowEnabled;

uniform sampler2D uCrateTex;
uniform sampler2D uGroundTex;
uniform vec2      uUVScale;

uniform sampler2D uSSAO;
uniform sampler2D uSH;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 gAlbedo;
layout(location = 2) out vec4 gNormal;
layout(location = 3) out vec4 gEmissive;

const float PI = 3.14159265359;

void sh_basis(vec3 d, out float Y[9]) {
    Y[0] = 0.282095;
    Y[1] = 0.488603 * d.y;
    Y[2] = 0.488603 * d.z;
    Y[3] = 0.488603 * d.x;
    Y[4] = 1.092548 * d.x * d.y;
    Y[5] = 1.092548 * d.y * d.z;
    Y[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
    Y[7] = 1.092548 * d.x * d.z;
    Y[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

vec3 sh_irradiance(vec3 n) {
    float Y[9];
    sh_basis(n, Y);
    const float A0 = 3.14159265;
    const float A1 = 2.09439510;
    const float A2 = 0.78539816;
    vec3 E = vec3(0.0);
    for (int i = 0; i < 9; ++i) {
        vec3 L = texelFetch(uSH, ivec2(i, 0), 0).rgb;
        float a = (i == 0) ? A0 : (i < 4) ? A1 : A2;
        E += L * Y[i] * a;
    }
    return max(E, vec3(0.0));
}

int pickCascade(float viewDepth) {
    if (viewDepth < uCascadeSplits[0]) return 0;
    if (viewDepth < uCascadeSplits[1]) return 1;
    return 2;
}

float shadowPCF(int cascade, vec3 worldPos, float bias) {
    vec4 lsPos = uLightSpace[cascade] * vec4(worldPos, 1.0);
    vec3 proj  = lsPos.xyz / lsPos.w * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;

    float sum = 0.0;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y) {
            vec2 off = vec2(float(x), float(y)) * uShadowTexel;
            float d = texture(uShadowMap, vec3(proj.xy + off, float(cascade))).r;
            sum += (proj.z - bias <= d) ? 1.0 : 0.0;
        }
    return sum / 25.0;
}

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float G_SchlickGGX(float NdotV, float k) {
    return NdotV / (NdotV * (1.0 - k) + k);
}
float G_Smith(float NdotV, float NdotL, float rough) {
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}
vec3 F_Schlick(vec3 F0, float VdotH) {
    return F0 + (vec3(1.0) - F0) * pow(1.0 - VdotH, 5.0);
}
vec3 evalSpecular(vec3 F0, float rough, float NdotV, float NdotL, float NdotH, float VdotH) {
    float a = rough * rough;
    float D = D_GGX(NdotH, a);
    float G = G_Smith(NdotV, NdotL, rough);
    vec3  F = F_Schlick(F0, VdotH);
    return (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
}

void main() {
    vec3 albedo = vColor;
    float roughness = 0.7;
    if (vMaterial > 1.5) {
        albedo = texture(uGroundTex, vUV * uUVScale).rgb;
        roughness = 0.95;
    } else if (vMaterial > 0.5) {
        albedo = texture(uCrateTex, vUV).rgb;
        roughness = 0.85;
    }
    float metallic = 0.0;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 L = normalize(-uLightDir);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 kD = (vec3(1.0) - F_Schlick(F0, VdotH)) * (1.0 - metallic);

    float shadow = 1.0;
    if (uShadowEnabled == 1) {
        int cascade = pickCascade(vViewDepth);
        float bias = max(0.0025 * (1.0 - NdotL), 0.0008);
        shadow = shadowPCF(cascade, vWorldPos, bias);
    }

    vec3 radiance = uLightColor * uLightIntensity;
    vec3 diffuse  = kD * albedo / PI * radiance;
    vec3 specular = evalSpecular(F0, roughness, NdotV, NdotL, NdotH, VdotH) * radiance;
    vec3 direct   = (diffuse + specular) * NdotL * shadow;

    vec2 ao_uv = gl_FragCoord.xy / vec2(textureSize(uSSAO, 0));
    float ao = texture(uSSAO, ao_uv).r;

    vec3 E_ambient = sh_irradiance(N);
    vec3 ambient = kD * albedo / PI * E_ambient * ao;

    vec3 color = direct + ambient;

    fragColor = vec4(color, 1.0);
    gAlbedo   = vec4(albedo, 1.0);
    gNormal   = vec4(N * 0.5 + 0.5, roughness);
    gEmissive = vec4(0.0, 0.0, 0.0, 1.0);
}
