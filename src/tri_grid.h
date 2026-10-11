#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

// Spatial grid of triangles for the SG probe bake. Stores three textures:
//   tri_tex  : RGBA32F, tex_w x tex_h. Each triangle occupies 3 texels
//              (one per vertex). vertex .xyz = position, .w = albedo
//              (packed as the R channel per vertex, extended via albedos).
//   cell_tex : RGBA32F, (GRID_X*GRID_Y) x GRID_Z. Each texel = (start, count, 0, 0)
//              into the index array.
//   idx_tex  : R32UI, idx_tex_w x idx_tex_h. Flat list of triangle indices.
#define TG_GRID_X 32
#define TG_GRID_Y 16
#define TG_GRID_Z 32

struct TriGrid {
    GLuint tri_tex  = 0;
    GLuint cell_tex = 0;
    GLuint idx_tex  = 0;

    int    tri_count = 0;
    int    grid_x = TG_GRID_X, grid_y = TG_GRID_Y, grid_z = TG_GRID_Z;
    int    cell_count = 0;
    int    total_entries = 0;

    int    tri_tex_w = 0, tri_tex_h = 0, tri_per_row = 0;
    int    idx_tex_w = 0, idx_tex_h = 0;

    glm::vec3 grid_min{0}, grid_max{0};
};

TriGrid tri_grid_build(const std::vector<float>& positions,   // 9 floats per triangle
                       const std::vector<float>& albedos,     // 3 floats per triangle
                       int count,
                       const glm::vec3& world_min,
                       const glm::vec3& world_max);

void tri_grid_destroy(TriGrid* tg);
