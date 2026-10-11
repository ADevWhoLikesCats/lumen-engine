#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

#define CSM_CASCADE_COUNT 3

// Cascaded shadow map: 3 depth-only layers in a single 2D array texture.
struct ShadowMap {
    GLuint fbo      = 0;
    GLuint depthTex = 0;
    GLsizei size    = 2048;

    // Far view-depth of each cascade (metres). Cascade 0 covers 0..splits[0].
    float splits[CSM_CASCADE_COUNT] = { 20.0f, 50.0f, 140.0f };

    // Per-cascade light-space matrix (proj * view)
    glm::mat4 lightSpace[CSM_CASCADE_COUNT];

    void init() {
        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, depthTex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                     size, size, CSM_CASCADE_COUNT,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {1,1,1,1};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  depthTex, 0, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            fprintf(stderr, "ShadowMap CSM FBO incomplete\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    // Update all cascade matrices from the camera's view and projection.
    // lightDir points FROM the light (normalized).
    void update(const glm::mat4& camView, const glm::mat4& camProj,
                const glm::vec3& lightDir, float aspect, float fovY, float nearP)
    {
        // Inverse of proj to get the four far-plane corners of the view frustum.
        glm::mat4 invVP = glm::inverse(camProj * camView);

        // Compute corners in world space for each cascade's slice.
        float prevSplit = nearP;
        for (int c = 0; c < CSM_CASCADE_COUNT; ++c) {
            float farC = splits[c];

            // NDC corners at near/far slice of this cascade.
            glm::vec4 corners[8] = {
                { -1, -1, -1, 1 }, {  1, -1, -1, 1 }, {  1,  1, -1, 1 }, { -1,  1, -1, 1 },
                { -1, -1,  1, 1 }, {  1, -1,  1, 1 }, {  1,  1,  1, 1 }, { -1,  1,  1, 1 },
            };
            // Remap NDC z from [0..1] to the actual slice range.
            // Simpler: project near/far slice points by manipulating z.
            // For ortho CSM we don't need exact NDC; we can just fit a bounding
            // sphere around the slice's center. That's more robust.

            // Center of the slice in world space = camera pos + forward * mid.
            float mid = (prevSplit + farC) * 0.5f;
            float radius = (farC - prevSplit) * 0.75f;   // slight padding

            // Camera position from view inverse
            glm::mat4 invView = glm::inverse(camView);
            glm::vec3 camPos = glm::vec3(invView[3]);
            glm::vec3 camFwd = -glm::normalize(glm::vec3(camView[0][2], camView[1][2], camView[2][2]));
            glm::vec3 sliceCenter = camPos + camFwd * mid;

            // Build light-space ortho box around the slice.
            glm::vec3 up(0,1,0);
            if (fabsf(glm::dot(lightDir, up)) > 0.99f) up = glm::vec3(0,0,1);
            glm::vec3 lightPos = sliceCenter - lightDir * (radius * 4.0f);
            glm::mat4 lightView = glm::lookAt(lightPos, sliceCenter, up);
            glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius,
                                             0.1f, radius * 8.0f);

            lightSpace[c] = lightProj * lightView;
            prevSplit = farC;
        }
    }

    void bindForWriting(int cascade) const {
        glViewport(0, 0, size, size);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  depthTex, 0, cascade);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);
    }

    void unbindForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glCullFace(GL_BACK);
    }

    void bindForReading(int unit) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, depthTex);
    }
};
