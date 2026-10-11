#include "framebuffer.h"
#include <stdio.h>
#include <stdlib.h>

static void to_gl_format(FBFormat f, GLenum* internal, GLenum* type, GLenum* format) {
    switch (f) {
        case FB_RGBA8:   *internal = GL_RGBA8;   *type = GL_UNSIGNED_BYTE; *format = GL_RGBA; return;
        case FB_RGBA16F: *internal = GL_RGBA16F; *type = GL_FLOAT;         *format = GL_RGBA; return;
        case FB_RGBA32F: *internal = GL_RGBA32F; *type = GL_FLOAT;         *format = GL_RGBA; return;
        case FB_R8:      *internal = GL_R8;      *type = GL_UNSIGNED_BYTE; *format = GL_RED;  return;
    }
    *internal = GL_RGBA8; *type = GL_UNSIGNED_BYTE; *format = GL_RGBA;
}

static void attach_color(GLuint tex, GLenum attachment) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, tex, 0);
}

Framebuffer fb_create(int width, int height, FBFormat fmt) {
    Framebuffer fb = {0};
    fb.width = width;
    fb.height = height;
    fb.format = fmt;
    fb.attachment_count = 1;
    fb.owns_depth = 1;

    GLenum internal, type, format;
    to_gl_format(fmt, &internal, &type, &format);

    glGenTextures(1, &fb.color);
    glBindTexture(GL_TEXTURE_2D, fb.color);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, format, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &fb.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, fb.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    attach_color(fb.color, GL_COLOR_ATTACHMENT0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.depth);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[fb] incomplete (0x%04X) %dx%d fmt=%d\n", status, width, height, (int)fmt);
        abort();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    return fb;
}

Framebuffer fb_create_with_depth_tex(int width, int height, FBFormat fmt) {
    Framebuffer fb = {};
    fb.width = width;
    fb.height = height;
    fb.format = fmt;
    fb.attachment_count = 1;
    fb.owns_depth = 1;

    GLenum internal, type, format;
    to_gl_format(fmt, &internal, &type, &format);

    glGenTextures(1, &fb.color);
    glBindTexture(GL_TEXTURE_2D, fb.color);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, format, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &fb.depth);
    glBindTexture(GL_TEXTURE_2D, fb.depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    attach_color(fb.color, GL_COLOR_ATTACHMENT0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, fb.depth, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[fb] depth-tex incomplete (0x%04X)\n", status);
        abort();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return fb;
}

Framebuffer fb_create_gbuffer(int width, int height, GLuint depth_tex) {
    Framebuffer fb = {0};
    fb.width = width;
    fb.height = height;
    fb.format = FB_RGBA16F;
    fb.attachment_count = 4;

    GLuint tex[4] = {0,0,0,0};
    glGenTextures(4, tex);
    for (int i = 0; i < 4; ++i) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                     GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    fb.color  = tex[0];
    fb.color1 = tex[1];
    fb.color2 = tex[2];
    fb.color3 = tex[3];

    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    attach_color(tex[0], GL_COLOR_ATTACHMENT0);
    attach_color(tex[1], GL_COLOR_ATTACHMENT1);
    attach_color(tex[2], GL_COLOR_ATTACHMENT2);
    attach_color(tex[3], GL_COLOR_ATTACHMENT3);

    if (depth_tex) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex, 0);
        fb.depth = 0;
        fb.owns_depth = 0;
    } else {
        glGenRenderbuffers(1, &fb.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, fb.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.depth);
        fb.owns_depth = 1;
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[gbuffer] incomplete (0x%04X) %dx%d\n", status, width, height);
        abort();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    fprintf(stderr, "[gbuffer] created %dx%d att0=%u att1=%u att2=%u att3=%u\n",
            width, height, tex[0], tex[1], tex[2], tex[3]);
    return fb;
}

void fb_destroy(Framebuffer* fb) {
    if (!fb) return;
    if (fb->color)  glDeleteTextures(1, &fb->color);
    if (fb->color1) glDeleteTextures(1, &fb->color1);
    if (fb->color2) glDeleteTextures(1, &fb->color2);
    if (fb->color3) glDeleteTextures(1, &fb->color3);
    if (fb->depth && fb->owns_depth) {
        // Could be a texture or renderbuffer — the caller knows. We'll just
        // guess based on attachment_count. For G-buffers depth is a texture.
        if (fb->attachment_count == 4) glDeleteTextures(1, &fb->depth);
        else glDeleteRenderbuffers(1, &fb->depth);
    }
    if (fb->fbo) glDeleteFramebuffers(1, &fb->fbo);
    fb->color = fb->color1 = fb->color2 = fb->color3 = fb->depth = fb->fbo = 0;
}

void fb_bind(Framebuffer* fb) {
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glViewport(0, 0, fb->width, fb->height);
}

void fb_bind_read(Framebuffer* fb, int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, fb->color);
}

void fb_unbind(int screen_w, int screen_h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_w, screen_h);
}
