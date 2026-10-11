#pragma once
#include <glad/glad.h>

typedef enum {
    FB_RGBA8,
    FB_RGBA16F,
    FB_RGBA32F,
    FB_R8,
} FBFormat;

typedef struct {
    GLuint   fbo;
    GLuint   color;       // attachment 0
    GLuint   color1;      // attachment 1
    GLuint   color2;      // attachment 2
    GLuint   color3;      // attachment 3
    GLuint   depth;
    int      width, height;
    FBFormat format;
    int      attachment_count;
    int      owns_depth;  // false if depth is borrowed
} Framebuffer;

Framebuffer fb_create(int width, int height, FBFormat fmt);
Framebuffer fb_create_with_depth_tex(int width, int height, FBFormat fmt);
Framebuffer fb_create_gbuffer(int width, int height, GLuint depth_tex);

void fb_destroy(Framebuffer* fb);
void fb_bind(Framebuffer* fb);
void fb_bind_read(Framebuffer* fb, int unit);
void fb_unbind(int screen_w, int screen_h);
