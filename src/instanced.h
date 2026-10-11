#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <algorithm>
#include "mesh.h"

// One mesh, drawn N times with per-instance model matrix, color, and material flag.
struct InstancedMesh {
    GLuint vao = 0;
    GLuint instanceVBO = 0;
    GLsizei indexCount = 0;
    GLsizei instanceCount = 0;

    void init(const Mesh& src, size_t maxInstances) {
        vao = src.vao;
        indexCount = src.indexCount;

        glBindVertexArray(vao);

        // 16 floats matrix + 3 floats color + 1 float material = 20
        const GLsizei stride = sizeof(float) * 20;
        glGenBuffers(1, &instanceVBO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(float) * 20 * maxInstances,
                     nullptr, GL_DYNAMIC_DRAW);

        // Locations 3..6: model matrix columns.
        for (int i = 0; i < 4; ++i) {
            GLuint l = 3 + i;
            glEnableVertexAttribArray(l);
            glVertexAttribPointer(l, 4, GL_FLOAT, GL_FALSE, stride,
                                  (void*)(sizeof(float) * i * 4));
            glVertexAttribDivisor(l, 1);
        }
        // Location 7: base color.
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, stride,
                              (void*)(sizeof(float) * 16));
        glVertexAttribDivisor(7, 1);

        // Location 8: material flag (0 = flat color, 1 = crate texture).
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, stride,
                              (void*)(sizeof(float) * 19));
        glVertexAttribDivisor(8, 1);

        glBindVertexArray(0);
    }

    void upload(const std::vector<glm::mat4>& models,
                const std::vector<glm::vec3>& colors,
                const std::vector<float>& materials) {
        instanceCount = (GLsizei)std::min({models.size(),
                                           colors.size(),
                                           materials.size()});
        if (instanceCount == 0) return;

        static std::vector<float> flat;   // reused across frames
        flat.clear();
        flat.reserve(instanceCount * 20);
        for (GLsizei i = 0; i < instanceCount; ++i) {
            const float* m = glm::value_ptr(models[i]);
            flat.insert(flat.end(), m, m + 16);
            const float* c = glm::value_ptr(colors[i]);
            flat.insert(flat.end(), c, c + 3);
            flat.push_back(materials[i]);
        }

        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        flat.size() * sizeof(float), flat.data());
    }

    void draw() const {
        if (instanceCount == 0) return;
        glBindVertexArray(vao);
        glDrawElementsInstanced(GL_TRIANGLES, indexCount,
                                GL_UNSIGNED_INT, nullptr, instanceCount);
        glBindVertexArray(0);
    }
};
