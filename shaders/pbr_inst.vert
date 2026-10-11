#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 iModel0;
layout(location=4) in vec4 iModel1;
layout(location=5) in vec4 iModel2;
layout(location=6) in vec4 iModel3;
layout(location=7) in vec3 iColor;
layout(location=8) in float iMaterial;

uniform mat4 uView;
uniform mat4 uPrevViewProj;
uniform mat4 uViewProj;
uniform mat4 uLightVP;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vColor;
out float vMaterial;
out vec4 vLightSpacePos;
out float vViewDepth;
out vec2 vPrevUV;

void main() {
    mat4 M = mat4(iModel0, iModel1, iModel2, iModel3);
    vec4 worldPos = M * vec4(aPos, 1.0);
    mat3 N = mat3(transpose(inverse(M)));

    vWorldPos = worldPos.xyz;
    vNormal = normalize(N * aNormal);
    vUV = aUV;
    vColor = iColor;
    vMaterial = iMaterial;
    vLightSpacePos = uLightVP * worldPos;

    vec4 prevClip = uPrevViewProj * worldPos;
    vPrevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    gl_Position = uViewProj * worldPos;
    vec4 viewPos = uView * worldPos;
    vViewDepth = -viewPos.z;
}
