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

uniform mat4 uView, uProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vColor;
out float vMaterial;

void main() {
    mat4 M = mat4(iModel0, iModel1, iModel2, iModel3);
    vec4 wp = M * vec4(aPos, 1.0);
    mat3 N = mat3(transpose(inverse(M)));
    vWorldPos = wp.xyz;
    vNormal   = normalize(N * aNormal);
    vUV       = aUV;
    vColor    = iColor;
    vMaterial = iMaterial;
    gl_Position = uProj * uView * wp;
}
