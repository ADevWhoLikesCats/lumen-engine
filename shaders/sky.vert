#version 330 core
// Full-screen triangle — no VBO, generated from gl_VertexID.
out vec2 vNDC;
void main() {
    // Big triangle covering the screen: (-1,-1), (3,-1), (-1,3)
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vNDC = p * 2.0 - 1.0;
    gl_Position = vec4(vNDC, 1.0, 1.0);   // z=1 → far plane
}
