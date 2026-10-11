#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

// One directional-light shadow map. Depth-only, ortho projection.
struct ShadowMap {
    GLuint fbo = 0;
    GLuint depthTex = 0;
    GLsizei size = 2048;
    glm::mat4 lightSpace{1.0f};

    // Half-extent of the world we cover (world is 96×96, so 48 covers it).
    // Bump this if you grow the map.
    void init(float worldHalfExtent = 60.0f) {
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &depthTex);

        glBindTexture(GL_TEXTURE_2D, depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                     size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {1,1,1,1};   // outside = fully lit (no shadow)
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

        // Enable hardware PCF comparison on the sampler.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, depthTex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            fprintf(stderr, "ShadowMap FBO incomplete\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Ortho light-space matrix covering a cube of ±worldHalfExtent.
        glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        glm::vec3 center(48, 0, 48);   // world center
        glm::vec3 eye = center - lightDir * worldHalfExtent * 2.0f;
        glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0,1,0));
        glm::mat4 proj = glm::ortho(-worldHalfExtent, worldHalfExtent,
                                    -worldHalfExtent, worldHalfExtent,
                                    0.1f, worldHalfExtent * 4.0f);
        lightSpace = proj * view;
    }

    void bindForWriting() const {
        glViewport(0, 0, size, size);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);   // front-face culling reduces acne
    }

    void unbindForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glCullFace(GL_BACK);
    }

    void bindForReading(int unit) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, depthTex);
    }

    const glm::mat4& matrix() const { return lightSpace; }
};
