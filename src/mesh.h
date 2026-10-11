#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cstddef>

// PBR material — pointers are owned by the loader's texture pool, not by
// this struct. A null pointer means "slot not present, use the factor".
struct Material {
    GLuint basecolor = 0;
    GLuint metallicRoughness = 0;
    GLuint normal = 0;
    GLuint emissive = 0;
    GLuint occlusion = 0;

    glm::vec3 baseColorFactor{1.f, 1.f, 1.f};
    float     metallicFactor  = 0.f;
    float     roughnessFactor = 1.f;
    float     normalScale     = 1.f;
    glm::vec3 emissiveFactor{0.f, 0.f, 0.f};
    float     occlusionStrength = 1.f;

    bool doubleSided = false;
};

struct Vertex { glm::vec3 pos; glm::vec3 nrm; glm::vec2 uv; 
    glm::vec4 tangent{0.f, 0.f, 0.f, 1.f};
};

struct Mesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei indexCount = 0;

    Mesh() = default;

    Mesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);

        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex,pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex,nrm));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex,uv));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex,tangent));

        glBindVertexArray(0);
        indexCount = (GLsizei)idx.size();
    }

    // Non-copyable (owns GL handles). Move-only.
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept { *this = std::move(o); }
    Mesh& operator=(Mesh&& o) noexcept {
        std::swap(vao, o.vao);
        std::swap(vbo, o.vbo);
        std::swap(ibo, o.ibo);
        std::swap(indexCount, o.indexCount);
        return *this;
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    }
};

// Primitive builders
Mesh makeCube();
Mesh makePlane(float size);
