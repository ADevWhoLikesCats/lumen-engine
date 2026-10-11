#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uViewProj;
uniform mat4 uPrevViewProj;
uniform mat4 uLightVP;
uniform mat3 uNormalMat;
uniform vec3 uBaseColor;
uniform float uMaterialOverride;
uniform vec2  uUVScale;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vColor;
out float vMaterial;
out vec4 vLightSpacePos;
out float vViewDepth;
out vec2 vPrevUV;
out vec3 vTangent;
out vec3 vBitangent;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    vNormal    = normalize(uNormalMat * aNormal);
    vTangent   = normalize(uNormalMat * aTangent.xyz);
    vBitangent = cross(vNormal, vTangent) * aTangent.w;

    vUV = aUV * uUVScale;
    vColor = uBaseColor;
    vMaterial = uMaterialOverride;
    vLightSpacePos = uLightVP * worldPos;

    vec4 viewPos = uView * worldPos;
    vViewDepth = -viewPos.z;

    vec4 prevClip = uPrevViewProj * worldPos;
    vPrevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    gl_Position = uViewProj * worldPos;
}
