#include "mesh.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cstddef>

// Primitive builders

Mesh makeCube() {
    Vertex v[24];
    glm::vec3 n[6] = {{ 0,0, 1},{ 0,0,-1},{ 1,0,0},{-1,0,0},{ 0,1,0},{ 0,-1,0}};
    glm::vec2 uvs[4] = {{0,0},{1,0},{1,1},{0,1}};
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        glm::vec3 a = n[f];
        glm::vec3 t = (fabsf(a.y) > 0.5f) ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
        glm::vec3 b = glm::cross(a, t);
        glm::vec3 c = glm::cross(a, b);
        glm::vec3 p[4] = { -b-c, b-c, b+c, -b+c };
        for (int i = 0; i < 4; ++i) {
            v[vi++] = { p[i]*0.5f + a*0.5f, a, uvs[i] };
        }
    }
    std::vector<Vertex> verts(v, v+24);
    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; ++f) {
        uint32_t o = f*4;
        idx.insert(idx.end(), {o,o+1,o+2, o,o+2,o+3});
    }
    return Mesh(verts, idx);
}

Mesh makePlane(float size) {
    float h = size * 0.5f;
    std::vector<Vertex> v = {
        {{-h,0,-h},{0,1,0},{0,0}},
        {{ h,0,-h},{0,1,0},{1,0}},
        {{ h,0, h},{0,1,0},{1,1}},
        {{-h,0, h},{0,1,0},{0,1}},
    };
    std::vector<uint32_t> i = {0,2,1, 0,3,2};   // CCW from above
    return Mesh(v, i);
}
