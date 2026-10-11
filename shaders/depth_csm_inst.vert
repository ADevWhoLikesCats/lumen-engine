#version 330 core
layout(location=0) in vec3 aPos;
layout(location=3) in vec4 iModel0;
layout(location=4) in vec4 iModel1;
layout(location=5) in vec4 iModel2;
layout(location=6) in vec4 iModel3;
uniform mat4 uLightSpace;
void main() {
    mat4 M = mat4(iModel0, iModel1, iModel2, iModel3);
    gl_Position = uLightSpace * M * vec4(aPos, 1.0);
}
