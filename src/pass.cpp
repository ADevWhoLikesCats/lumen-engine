#include "pass.h"
#include "asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void clear_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) {}
}

static GLuint compile_shader(GLenum type, const char* src, const char* path) {
    GLuint s = glCreateShader(type);
    if (!s) { fprintf(stderr, "pass: glCreateShader failed for %s\n", path); abort(); }
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "pass: shader compile failed [%s]:\n%s\n", path, log);
        abort();
    }
    return s;
}

static GLuint load_shader(GLenum type, const char* path) {
    size_t len = 0;
    char* src = asset_load(path, &len);
    if (!src) { fprintf(stderr, "pass: missing shader %s\n", path); abort(); }
    GLuint s = compile_shader(type, src, path);
    free(src);
    return s;
}

Pass pass_create(const char* vert_path, const char* frag_path) {
    clear_gl_errors();

    Pass p;
    memset(&p, 0, sizeof(p));

    GLuint vs = load_shader(GL_VERTEX_SHADER,   vert_path);
    GLuint fs = load_shader(GL_FRAGMENT_SHADER, frag_path);

    p.prog = glCreateProgram();
    glAttachShader(p.prog, vs);
    glAttachShader(p.prog, fs);
    glLinkProgram(p.prog);

    // Force link completion before querying status (some drivers defer).
    GLint n = 0;
    glGetProgramiv(p.prog, GL_ACTIVE_UNIFORMS, &n);

    GLint ok = 0;
    glGetProgramiv(p.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p.prog, sizeof(log), NULL, log);
        fprintf(stderr, "pass: link failed [%s + %s]:\n%s\n", vert_path, frag_path, log);
        abort();
    }

    // Don't delete shaders — some Intel drivers dislike it.
    (void)vs; (void)fs;

    glGenVertexArrays(1, &p.vao);

    // Core profile requires at least one enabled attribute for a draw call
    // to rasterize, even for gl_VertexID-only shaders.
    glBindVertexArray(p.vao);
    glGenBuffers(1, &p.dummy_vbo);
    static const float dummy_pos[3] = { 0.0f, 0.0f, 0.0f };
    glBindBuffer(GL_ARRAY_BUFFER, p.dummy_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(dummy_pos), dummy_pos, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    clear_gl_errors();
    fprintf(stderr, "[pass] %s + %s -> prog=%u vao=%u\n",
            vert_path, frag_path, p.prog, p.vao);
    return p;
}

void pass_destroy(Pass* p) {
    if (!p) return;
    if (p->prog) glDeleteProgram(p->prog);
    if (p->vao)  glDeleteVertexArrays(1, &p->vao);
    if (p->dummy_vbo) glDeleteBuffers(1, &p->dummy_vbo);
    p->prog = 0;
    p->vao = 0;
    p->dummy_vbo = 0;
}

void pass_use(Pass* p) {
    // VAO-DBG: drain errors first, then check each step
    while (glGetError() != GL_NO_ERROR) {}

    glUseProgram(p->prog);
    GLenum e1 = glGetError();

    GLint vaoExists = glIsVertexArray(p->vao);
    glBindVertexArray(p->vao);
    GLenum e2 = glGetError();

    if (e1 != GL_NO_ERROR || e2 != GL_NO_ERROR) {
        fprintf(stderr, "pass_use DBG: prog=%u vao=%u isValid=%d e_prog=0x%04X e_vao=0x%04X\n",
                p->prog, p->vao, vaoExists, e1, e2);
        while (glGetError() != GL_NO_ERROR) {}
    }
}

void pass_draw(Pass* p, int width, int height) {
    (void)p; (void)width; (void)height;
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void pass_set_i32(Pass* p, const char* name, int v) {
    glUseProgram(p->prog);
    GLint loc = glGetUniformLocation(p->prog, name);
    if (loc >= 0) glUniform1i(loc, v);
}
void pass_set_f32(Pass* p, const char* name, float v) {
    glUseProgram(p->prog);
    GLint loc = glGetUniformLocation(p->prog, name);
    if (loc >= 0) glUniform1f(loc, v);
}
void pass_set_vec2(Pass* p, const char* name, const glm::vec2& v) {
    glUseProgram(p->prog);
    GLint loc = glGetUniformLocation(p->prog, name);
    if (loc >= 0) glUniform2f(loc, v.x, v.y);
}
void pass_set_vec3(Pass* p, const char* name, const glm::vec3& v) {
    glUseProgram(p->prog);
    GLint loc = glGetUniformLocation(p->prog, name);
    if (loc >= 0) glUniform3f(loc, v.x, v.y, v.z);
}
void pass_set_vec4(Pass* p, const char* name, const glm::vec4& v) {
    glUseProgram(p->prog);
    GLint loc = glGetUniformLocation(p->prog, name);
    if (loc >= 0) glUniform4f(loc, v.x, v.y, v.z, v.w);
}
void pass_set_mat4(Pass* p, const char* name, const glm::mat4& m) {
    glUseProgram(p->prog);
    GLint loc = glGetUniformLocation(p->prog, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(m));
}
void pass_set_tex(Pass* p, const char* name, int unit) {
    glUseProgram(p->prog);
    GLint loc = glGetUniformLocation(p->prog, name);
    if (loc >= 0) glUniform1i(loc, unit);
}

void check_gl_errors(const char* label) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[GL error after %s] 0x%04X\n", label, err);
        while (glGetError() != GL_NO_ERROR) {}
    }
}
