#include "tri_grid.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static inline int cell_index(int x, int y, int z) {
    return x + TG_GRID_X * (y + TG_GRID_Y * z);
}

TriGrid tri_grid_build(const std::vector<float>& tri_positions,
                       const std::vector<float>& tri_albedos,
                       int count,
                       const glm::vec3& world_min, const glm::vec3& world_max)
{
    TriGrid tg = {};
    tg.tri_count = count;
    tg.cell_count = TG_GRID_X * TG_GRID_Y * TG_GRID_Z;
    tg.grid_min = world_min;
    tg.grid_max = world_max;

    glm::vec3 span = world_max - world_min;
    span.x = std::max(span.x, 1e-4f);
    span.y = std::max(span.y, 1e-4f);
    span.z = std::max(span.z, 1e-4f);

    std::fprintf(stderr, "tri_grid: building %dx%dx%d over %d tris\n",
                 TG_GRID_X, TG_GRID_Y, TG_GRID_Z, count);

    std::vector<int>  cell_counts(tg.cell_count, 0);
    std::vector<std::vector<int>> tri_cells(count);

    int total_entries = 0;
    for (int i = 0; i < count; ++i) {
        const float* p = &tri_positions[i * 9];
        float mnx = p[0], mny = p[1], mnz = p[2];
        float mxx = p[0], mxy = p[1], mxz = p[2];
        for (int v = 1; v < 3; ++v) {
            mnx = std::min(mnx, p[v*3+0]); mny = std::min(mny, p[v*3+1]); mnz = std::min(mnz, p[v*3+2]);
            mxx = std::max(mxx, p[v*3+0]); mxy = std::max(mxy, p[v*3+1]); mxz = std::max(mxz, p[v*3+2]);
        }
        int cx0 = (int)((mnx - world_min.x) / span.x * TG_GRID_X);
        int cy0 = (int)((mny - world_min.y) / span.y * TG_GRID_Y);
        int cz0 = (int)((mnz - world_min.z) / span.z * TG_GRID_Z);
        int cx1 = (int)((mxx - world_min.x) / span.x * TG_GRID_X);
        int cy1 = (int)((mxy - world_min.y) / span.y * TG_GRID_Y);
        int cz1 = (int)((mxz - world_min.z) / span.z * TG_GRID_Z);
        cx0 = std::max(cx0, 0); cy0 = std::max(cy0, 0); cz0 = std::max(cz0, 0);
        cx1 = std::min(cx1, TG_GRID_X-1);
        cy1 = std::min(cy1, TG_GRID_Y-1);
        cz1 = std::min(cz1, TG_GRID_Z-1);

        for (int cz = cz0; cz <= cz1; ++cz)
            for (int cy = cy0; cy <= cy1; ++cy)
                for (int cx = cx0; cx <= cx1; ++cx)
                    tri_cells[i].push_back(cell_index(cx, cy, cz));

        for (int c : tri_cells[i]) cell_counts[c]++;
        total_entries += (int)tri_cells[i].size();
    }
    tg.total_entries = total_entries;

    std::vector<int> cell_start(tg.cell_count);
    int running = 0;
    for (int c = 0; c < tg.cell_count; ++c) { cell_start[c] = running; running += cell_counts[c]; }

    std::vector<unsigned int> indices(total_entries);
    std::vector<int> cursor = cell_start;
    for (int i = 0; i < count; ++i)
        for (int c : tri_cells[i]) indices[cursor[c]++] = (unsigned int)i;

    // Triangle texture
    int tris_per_row = 128;
    int tex_w = tris_per_row * 3;
    int tex_h = (count + tris_per_row - 1) / tris_per_row;
    tg.tri_tex_w = tex_w;
    tg.tri_tex_h = tex_h;
    tg.tri_per_row = tris_per_row;

    std::vector<float> tri_data(tex_w * tex_h * 4, 0.f);
    for (int i = 0; i < count; ++i) {
        const float* p = &tri_positions[i * 9];
        const float* alb = &tri_albedos[i * 3];
        int row = i / tris_per_row;
        int col = (i % tris_per_row) * 3;
        int base = (row * tex_w + col) * 4;
        tri_data[base+0]=p[0]; tri_data[base+1]=p[1]; tri_data[base+2]=p[2]; tri_data[base+3]=alb[0];
        tri_data[base+4]=p[3]; tri_data[base+5]=p[4]; tri_data[base+6]=p[5]; tri_data[base+7]=alb[1];
        tri_data[base+8]=p[6]; tri_data[base+9]=p[7]; tri_data[base+10]=p[8]; tri_data[base+11]=alb[2];
    }
    glGenTextures(1, &tg.tri_tex);
    glBindTexture(GL_TEXTURE_2D, tg.tri_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tex_w, tex_h, 0, GL_RGBA, GL_FLOAT, tri_data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Cell texture
    int cell_tex_w = TG_GRID_X * TG_GRID_Y;
    int cell_tex_h = TG_GRID_Z;
    std::vector<float> cell_data(cell_tex_w * cell_tex_h * 4, 0.f);
    for (int z = 0; z < TG_GRID_Z; ++z)
        for (int y = 0; y < TG_GRID_Y; ++y)
            for (int x = 0; x < TG_GRID_X; ++x) {
                int c = cell_index(x, y, z);
                int col = y * TG_GRID_X + x;
                int base = (z * cell_tex_w + col) * 4;
                cell_data[base+0] = (float)cell_start[c];
                cell_data[base+1] = (float)cell_counts[c];
            }
    glGenTextures(1, &tg.cell_tex);
    glBindTexture(GL_TEXTURE_2D, tg.cell_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, cell_tex_w, cell_tex_h, 0, GL_RGBA, GL_FLOAT, cell_data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Index texture (R32UI)
    int idx_w = 4096;
    int idx_h = (total_entries + idx_w - 1) / idx_w;
    tg.idx_tex_w = idx_w;
    tg.idx_tex_h = idx_h;
    std::vector<unsigned int> idx_padded(idx_w * idx_h, 0);
    std::memcpy(idx_padded.data(), indices.data(), sizeof(unsigned int) * total_entries);
    glGenTextures(1, &tg.idx_tex);
    glBindTexture(GL_TEXTURE_2D, tg.idx_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, idx_w, idx_h, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, idx_padded.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::fprintf(stderr, "tri_grid: %d entries, %dx%d tri tex\n",
                 total_entries, tex_w, tex_h);
    return tg;
}

void tri_grid_destroy(TriGrid* tg) {
    if (tg->tri_tex)  glDeleteTextures(1, &tg->tri_tex);
    if (tg->cell_tex) glDeleteTextures(1, &tg->cell_tex);
    if (tg->idx_tex)  glDeleteTextures(1, &tg->idx_tex);
    tg->tri_tex = tg->cell_tex = tg->idx_tex = 0;
}
