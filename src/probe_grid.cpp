#include "probe_grid.h"
#include <cstdio>

ProbeGrid probe_grid_create(const glm::vec3& gmin, const glm::vec3& gmax) {
    ProbeGrid pg = {};
    pg.grid_min = gmin;
    pg.grid_max = gmax;
    pg.fb = fb_create(2 * PROBE_SG_COUNT, PROBE_COUNT, FB_RGBA32F);
    pg.bake_pass = pass_create("shaders/fullscreen.vert", "shaders/probe_bake.frag");
    pg.valid = false;
    return pg;
}

void probe_grid_destroy(ProbeGrid* pg) {
    fb_destroy(&pg->fb);
    pass_destroy(&pg->bake_pass);
}

void probe_grid_bake(ProbeGrid* pg, TriGrid* tg,
                     const glm::vec3& car_grid_min, const glm::vec3& car_grid_max,
                     float ground_y, const glm::vec3& ground_albedo,
                     const glm::vec3& sky_horizon, const glm::vec3& sky_zenith,
                     const glm::vec3& light_dir, const glm::vec3& light_color,
                     float light_intensity)
{
    fb_bind(&pg->fb);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);

    pass_use(&pg->bake_pass);
    pass_set_vec3(&pg->bake_pass, "uGridMin", pg->grid_min);
    pass_set_vec3(&pg->bake_pass, "uGridMax", pg->grid_max);
    pass_set_vec3(&pg->bake_pass, "uCarGridMin", car_grid_min);
    pass_set_vec3(&pg->bake_pass, "uCarGridMax", car_grid_max);
    pass_set_f32 (&pg->bake_pass, "uGroundY", ground_y);
    pass_set_vec3(&pg->bake_pass, "uGroundAlbedo", ground_albedo);
    pass_set_vec3(&pg->bake_pass, "uSkyHorizon", sky_horizon);
    pass_set_vec3(&pg->bake_pass, "uSkyZenith",  sky_zenith);
    pass_set_vec3(&pg->bake_pass, "uLightDir",   light_dir);
    pass_set_vec3(&pg->bake_pass, "uLightColor", light_color);
    pass_set_f32 (&pg->bake_pass, "uLightIntensity", light_intensity);
    pass_set_i32 (&pg->bake_pass, "uTriPerRow", tg->tri_per_row);
    pass_set_f32 (&pg->bake_pass, "uIdxTexW",   (float)tg->idx_tex_w);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tg->tri_tex);
    pass_set_i32(&pg->bake_pass, "uTriTex", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tg->cell_tex);
    pass_set_i32(&pg->bake_pass, "uCellTex", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tg->idx_tex);
    pass_set_i32(&pg->bake_pass, "uIdxTex", 2);

    glActiveTexture(GL_TEXTURE0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    pg->valid = true;
    std::fprintf(stderr, "[probe] baked %d probes, %d SGs each\n",
                 PROBE_COUNT, PROBE_SG_COUNT);
}
