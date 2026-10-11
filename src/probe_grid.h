#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "framebuffer.h"
#include "pass.h"
#include "tri_grid.h"

#define PROBE_COUNT_X 8
#define PROBE_COUNT_Y 4
#define PROBE_COUNT_Z 8
#define PROBE_COUNT   (PROBE_COUNT_X * PROBE_COUNT_Y * PROBE_COUNT_Z)
#define PROBE_SG_COUNT 32
#define PROBE_SG_SHARPNESS 4.0f

struct ProbeGrid {
    Framebuffer fb;         // (2*PROBE_SG_COUNT) x PROBE_COUNT, RGBA32F
    Pass        bake_pass;
    glm::vec3   grid_min{0};
    glm::vec3   grid_max{0};
    bool        valid = false;
};

ProbeGrid probe_grid_create(const glm::vec3& gmin, const glm::vec3& gmax);
void      probe_grid_destroy(ProbeGrid* pg);

void      probe_grid_bake(ProbeGrid* pg, TriGrid* tg,
                          const glm::vec3& car_grid_min, const glm::vec3& car_grid_max,
                          float ground_y, const glm::vec3& ground_albedo,
                          const glm::vec3& sky_horizon, const glm::vec3& sky_zenith,
                          const glm::vec3& light_dir, const glm::vec3& light_color,
                          float light_intensity);
