#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

typedef struct {
    GLuint prog;
    GLuint vao;      // empty VAO, needs to exist for core profile
    GLuint dummy_vbo; // referenced by vao; do not delete
} Pass;

Pass pass_create(const char* vert_path, const char* frag_path);
void pass_destroy(Pass* p);

void pass_use(Pass* p);
void pass_draw(Pass* p, int width, int height);

void pass_set_i32 (Pass* p, const char* name, int v);
void pass_set_f32 (Pass* p, const char* name, float v);
void pass_set_vec2(Pass* p, const char* name, const glm::vec2& v);
void pass_set_vec3(Pass* p, const char* name, const glm::vec3& v);
void pass_set_vec4(Pass* p, const char* name, const glm::vec4& v);
void pass_set_mat4(Pass* p, const char* name, const glm::mat4& m);
void pass_set_tex (Pass* p, const char* name, int unit);

void check_gl_errors(const char* label);
