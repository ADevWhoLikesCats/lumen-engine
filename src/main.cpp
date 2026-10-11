// ---------------------------------------------------------------
// lumen-engine — deferred G-buffer pipeline (Phase 1)
// Geometry pass writes to a 4-attachment G-buffer.
// The result is tonemapped to the backbuffer.
// ---------------------------------------------------------------
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "pass.h"
#include "framebuffer.h"
#include "tri_grid.h"
#include "probe_grid.h"
#include "mesh.h"
#include "glb_loader.h"
#include "instanced.h"
#include "shadowmap.h"
#include "texture.h"
#include "map.h"
#include "collider.h"
#include "controller.h"

#include <vector>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

// ===============================================================
// Shader helper — reads source and compiles through Pass
// ===============================================================
static std::string readFile(const char* path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Failed to open " << path << "\n"; std::exit(1); }
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

static GLuint compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log);
        std::cerr << "Shader compile failed:\n" << log << "\n";
        std::exit(1);
    }
    return s;
}

// Simple uniform-cached shader (for the geometry pass — separate from Pass).
struct Shader {
    GLuint id = 0;
    mutable std::unordered_map<std::string, GLint> cache;

    Shader(const char* vsPath, const char* fsPath) {
        GLuint vs = compile(GL_VERTEX_SHADER,   readFile(vsPath));
        GLuint fs = compile(GL_FRAGMENT_SHADER, readFile(fsPath));
        id = glCreateProgram();
        glAttachShader(id, vs); glAttachShader(id, fs);
        glLinkProgram(id);
        GLint ok; glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024]; glGetProgramInfoLog(id, 1024, nullptr, log);
            std::cerr << "Link failed:\n" << log << "\n"; std::exit(1);
        }
        glDeleteShader(vs); glDeleteShader(fs);
    }
    void use() const { glUseProgram(id); }

    GLint loc(const char* n) const {
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        GLint l = glGetUniformLocation(id, n);
        cache.emplace(n, l);
        return l;
    }
    void set(const char* n, float v)            const { glUniform1f(loc(n), v); }
    void set(const char* n, int v)              const { glUniform1i(loc(n), v); }
    void set(const char* n, const glm::vec2& v) const { glUniform2fv(loc(n),1,glm::value_ptr(v)); }
    void set(const char* n, const glm::vec3& v) const { glUniform3fv(loc(n),1,glm::value_ptr(v)); }
    void set(const char* n, const glm::mat3& m) const { glUniformMatrix3fv(loc(n),1,GL_FALSE,glm::value_ptr(m)); }
    void set(const char* n, const glm::mat4& m) const { glUniformMatrix4fv(loc(n),1,GL_FALSE,glm::value_ptr(m)); }
};

// ===============================================================
// Input
// ===============================================================
struct Input {
    bool firstMouse = true;
    double lastX = 0, lastY = 0;
    float dx = 0, dy = 0;
    bool w = false, a = false, s = false, d = false;
    bool shift = false;
};
static Input gInput;

static void key_cb(GLFWwindow*, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(glfwGetCurrentContext(), true);
    bool down = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_W) gInput.w = down;
    if (key == GLFW_KEY_A) gInput.a = down;
    if (key == GLFW_KEY_S) gInput.s = down;
    if (key == GLFW_KEY_D) gInput.d = down;
    if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) gInput.shift = down;
}

static void mouse_cb(GLFWwindow*, double x, double y) {
    if (gInput.firstMouse) { gInput.lastX = x; gInput.lastY = y; gInput.firstMouse = false; }
    gInput.dx += float(x - gInput.lastX);
    gInput.dy += float(y - gInput.lastY);
    gInput.lastX = x; gInput.lastY = y;
}

// ===============================================================
// Camera
// ===============================================================
struct Camera {
    glm::vec3 pos{0, 1.7f, 5};
    float yaw = -90.f, pitch = -30.f;
    float speed = 4.5f, sens = 0.12f;

    glm::vec3 forward() const {
        float cy = cosf(glm::radians(yaw));
        float sy = sinf(glm::radians(yaw));
        float cp = cosf(glm::radians(pitch));
        float sp = sinf(glm::radians(pitch));
        return glm::normalize(glm::vec3(cy*cp, sp, sy*cp));
    }
    glm::vec3 right() const { return glm::normalize(glm::cross(forward(), {0,1,0})); }
    void mouse(float dx, float dy) {
        yaw += dx * sens;
        pitch = glm::clamp(pitch - dy * sens, -89.f, 89.f);
    }
    glm::mat4 view() const { return glm::lookAt(pos, pos + forward(), {0,1,0}); }
};

// ===============================================================
// Halton sequence for TAA jitter
// ===============================================================
static float halton_seq(int index, int base) {
    float f = 1.0f, r = 0.0f;
    int i = index;
    while (i > 0) {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}
static void taa_jitter(int frame, float* jx, float* jy) {
    int idx = (frame % 8) + 1;
    *jx = halton_seq(idx, 2) - 0.5f;
    *jy = halton_seq(idx, 3) - 0.5f;
}

// ===============================================================
// main
// ===============================================================
int main() {
    if (!glfwInit()) { std::cerr << "glfwInit failed\n"; return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 0);   // deferred: MSAA off, TAA comes later

    GLFWwindow* win = glfwCreateWindow(1280, 720, "lumen-engine", nullptr, nullptr);
    if (!win) { std::cerr << "Window creation failed\n"; return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD load failed\n"; return 1;
    }

    glfwSetKeyCallback(win, key_cb);
    glfwSetCursorPosCallback(win, mouse_cb);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    std::cout << "GL " << glGetString(GL_VERSION) << "\n";

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // ---------- Assets ----------
    Texture floorTex("assets/ground.png");
    Texture crateTex("assets/crate.png");

    // Geometry: cube + plane
    Mesh cube  = makeCube();
    Mesh floor = makePlane(1.f);

    // ---------- Map ----------
    GeneratedMap map = generateMap(24, 24, 4.0f, 1337u);

    std::vector<OBB> world;
    {
        // Center the slab under the visual floor mesh, which sits at
        // (W*tile/2, 0, H*tile/2). Same center as the floor's visual transform.
        glm::vec3 ctr(map.W*map.tile*0.5f, -0.5f, map.H*map.tile*0.5f);
        glm::mat4 fm = glm::translate(glm::mat4(1.f), ctr)
                     * glm::scale(glm::mat4(1.f), glm::vec3(map.W*map.tile, 1.f, map.H*map.tile));
        world.push_back(OBB::fromMatrix(fm, glm::vec3(0.5f)));
    }

    // Scene: an Instance is mesh + transform + color + material flag
    struct Instance {
        const Mesh* mesh;
        glm::mat4   model;
        glm::vec3   color;
        const Texture* tex;
        glm::vec2   uvScale;
        float       material;
        const Material* mat = nullptr;   // PBR material, or null
    };
    std::vector<Instance> scene;

    auto add = [&](const Mesh& m, glm::mat4 xf, glm::vec3 c,
                   const Texture* t = nullptr, glm::vec2 uvs = {1,1}, float mat = 0.f,
                   const Material* pbr = nullptr) {
        scene.push_back({&m, xf, c, t, uvs, mat, pbr});
    };

    // Floor (textured, big quad)
    float floorTiles = (float)map.W * 2.0f;
    add(floor,
        glm::translate(glm::mat4(1.f), glm::vec3(map.W*map.tile*0.5f, 0.f, map.H*map.tile*0.5f))
      * glm::scale(glm::mat4(1.f), glm::vec3(map.W*map.tile, 1.f, map.H*map.tile)),
        {1,1,1}, &floorTex, glm::vec2(floorTiles, floorTiles), 2.0f);

    // Walls
    for (size_t i = 0; i < map.wallTransforms.size(); ++i) {
        add(cube, map.wallTransforms[i], {0.55f, 0.53f, 0.50f});
        world.push_back(map.wallColliders[i]);
    }

    // Props
    {
        std::mt19937 rng(1337u ^ 0x9E3779B9u);
        std::uniform_real_distribution<float> chance(0.f, 1.f);
        std::uniform_int_distribution<int>   kind(0, 2);
        std::uniform_real_distribution<float> jitter(-0.15f, 0.15f);
        for (int y = 0; y < map.H; ++y) {
            for (int x = 0; x < map.W; ++x) {
                if (!map.walkable(x, y)) continue;
                if (chance(rng) > 0.15f) continue;
                glm::vec3 c = map.cellCenter(x, y);
                if (glm::distance(c, map.spawn) < 2.0f) continue;
                c.x += jitter(rng);
                c.z += jitter(rng);

                glm::vec3 half, color; float mat = 0.f;
                switch (kind(rng)) {
                    case 0: half={0.35f,0.35f,0.35f}; color={0.62f,0.42f,0.24f}; mat=1.f; break;
                    case 1: half={0.25f,1.00f,0.25f}; color={0.55f,0.52f,0.48f}; break;
                    default: half={0.35f,0.50f,0.35f}; color={0.32f,0.38f,0.42f}; break;
                }
                glm::vec3 pos = c + glm::vec3(0, half.y, 0);
                glm::mat4 m = glm::translate(glm::mat4(1.f), pos)
                            * glm::scale(glm::mat4(1.f), half * 2.f);
                add(cube, m, color, nullptr, {1,1}, mat);
                world.push_back(OBB::fromMatrix(m, glm::vec3(0.5f)));
            }
        }
    }

    // ---------- Load GLB model ----------
    std::vector<GLBMesh> helmet = loadGLB("assets/damaged_helmet.glb");
    for (auto& gm : helmet) {
        glm::mat4 xf = glm::translate(glm::mat4(1.f), glm::vec3(48.f, 2.f, 44.f))
                     * glm::scale(glm::mat4(1.f), glm::vec3(3.f, 3.f, 3.f));
        add(gm.mesh, xf, {1,1,1}, nullptr, {1,1}, 0.f, &gm.material);
    }

    // Split cube instances from non-cube instances
    std::vector<glm::mat4> cubeModels;
    std::vector<glm::vec3> cubeColors;
    std::vector<float>     cubeMaterials;
    for (auto& inst : scene) {
        if (inst.mesh == &cube) {
            cubeModels.push_back(inst.model);
            cubeColors.push_back(inst.color);
            cubeMaterials.push_back(inst.material);
        }
    }
    scene.erase(std::remove_if(scene.begin(), scene.end(),
        [&](const Instance& i){ return i.mesh == &cube; }), scene.end());

    InstancedMesh instancedCube;
    instancedCube.init(cube, cubeModels.size() + 16);

    // ---------- Shadow map ----------
    ShadowMap shadow;
    shadow.init();

    Shader depthInstShader ("shaders/depth_csm_inst.vert",  "shaders/depth.frag");
    Shader depthModelShader("shaders/depth_csm_model.vert", "shaders/depth.frag");

    // ---------- PBR geometry pass shaders ----------
    Shader pbrInstShader ("shaders/pbr_inst.vert",  "shaders/pbr.frag");
    Shader pbrModelShader("shaders/pbr_model.vert", "shaders/pbr.frag");

    // ---------- Tonemap pass ----------
    Pass tonemapPass = pass_create("shaders/tonemap.vert", "shaders/tonemap.frag");
    Pass ssaoPass      = pass_create("shaders/fullscreen.vert", "shaders/ssao.frag");
    Pass ssaoBlurPass  = pass_create("shaders/fullscreen.vert", "shaders/ssao_blur.frag");
    Pass ssgiPass      = pass_create("shaders/fullscreen.vert", "shaders/ssgi.frag");
    Pass taaPass       = pass_create("shaders/fullscreen.vert", "shaders/taa.frag");
    Pass fogRaymarchPass = pass_create("shaders/fullscreen.vert", "shaders/fog_raymarch.frag");
    Pass fogCompositePass = pass_create("shaders/fullscreen.vert", "shaders/fog_composite.frag");
    Pass ssrPass          = pass_create("shaders/fullscreen.vert", "shaders/ssr.frag");
    Pass ssrCompositePass = pass_create("shaders/fullscreen.vert", "shaders/ssr_composite.frag");
    Pass octaCapturePass = pass_create("shaders/fullscreen.vert", "shaders/octa_capture.frag");
    Pass shProjectPass   = pass_create("shaders/fullscreen.vert", "shaders/sh_project.frag");

    // ---------- Per-frame state ----------
    Camera cam;
    CharacterController player;
    player.pos = map.spawn;
    player.height = 1.7f;
    player.radius = 0.3f;
    float vy = 0.f;

    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.15f, -1.0f, -0.1f));
    glm::vec3 lightColor(1.0f, 0.96f, 0.90f);
    float     lightIntensity = 1.8f;

    Framebuffer octaCaptureFB = fb_create(128, 128, FB_RGBA16F);
    Framebuffer shFB         = fb_create(9, 1, FB_RGBA32F);
    Framebuffer sceneFB;   // HDR lit scene with a depth texture
    Framebuffer ssrFB;             // RGBA16F, rgb=reflection, a=hit confidence
    Framebuffer ssrCompositeFB;    // RGBA16F, lit + reflection
    Framebuffer fogFB;             // half-res fog (rgba=scatter, a=transmit)
    Framebuffer fogCompositeFB;    // full-res composited scene
    Framebuffer taaHistory[2];   // ping-pong
    int taaPing = 0;
    int frameIndex = 0;
    glm::mat4 prevViewProj(1.f);
    Framebuffer ssgiFB;    // RGBA16F indirect diffuse
    Framebuffer ssaoFB;    // R8 AO, raw
    Framebuffer ssaoBlurFB;// R8 AO, blurred
    Framebuffer gbuf;      // G-buffer (4 MRT attachments, borrowed depth)
    int lastW = 0, lastH = 0;

    auto ensureFB = [&](int w, int h) {
        if (w == lastW && h == lastH) return;
        lastW = w; lastH = h;
        if (sceneFB.fbo) fb_destroy(&sceneFB);
        if (gbuf.fbo)    fb_destroy(&gbuf);
    fb_destroy(&octaCaptureFB);
    fb_destroy(&shFB);
    if (ssaoFB.fbo)  fb_destroy(&ssaoFB);
    if (ssgiFB.fbo)  fb_destroy(&ssgiFB);
    if (fogFB.fbo)   fb_destroy(&fogFB);
    if (fogCompositeFB.fbo) fb_destroy(&fogCompositeFB);
    if (ssaoBlurFB.fbo) fb_destroy(&ssaoBlurFB);
        sceneFB     = fb_create_with_depth_tex(w, h, FB_RGBA16F);
        gbuf        = fb_create_gbuffer(w, h, sceneFB.depth);
        ssaoFB      = fb_create(w/2, h/2, FB_R8);
        ssgiFB      = fb_create(w/2, h/2, FB_RGBA16F);
        ssaoBlurFB  = fb_create(w/2, h/2, FB_R8);
        if (taaHistory[0].fbo) fb_destroy(&taaHistory[0]);
        if (taaHistory[1].fbo) fb_destroy(&taaHistory[1]);
        taaHistory[0] = fb_create(w, h, FB_RGBA16F);
        taaHistory[1] = fb_create(w, h, FB_RGBA16F);
        taaPing = 0;
        frameIndex = 0;
    };

    float lastTime = (float)glfwGetTime();
    // SH probe: run once at startup. Captures the sky + ground from a
    // probe at (48, 2, 48) — roughly the middle of the map.
    {
        glm::vec3 probePos(48.f, 2.f, 48.f);
        glm::vec3 skyHorizon(0.80f, 0.85f, 0.95f);
        glm::vec3 skyZenith (0.15f, 0.35f, 0.75f);
        glm::vec3 groundAlbedo(0.55f, 0.60f, 0.35f);

        fb_bind(&octaCaptureFB);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        pass_use(&octaCapturePass);
        pass_set_vec3(&octaCapturePass, "uProbePos", probePos);
        pass_set_vec3(&octaCapturePass, "uLightDir", lightDir);
        pass_set_vec3(&octaCapturePass, "uLightColor", lightColor);
        pass_set_f32 (&octaCapturePass, "uLightIntensity", lightIntensity);
        pass_set_vec2(&octaCapturePass, "uInvResolution", glm::vec2(1.f/128.f, 1.f/128.f));
        pass_set_f32 (&octaCapturePass, "uGroundY", 0.f);
        pass_set_vec3(&octaCapturePass, "uGroundAlbedo", groundAlbedo);
        pass_set_vec3(&octaCapturePass, "uSkyHorizon", skyHorizon);
        pass_set_vec3(&octaCapturePass, "uSkyZenith",  skyZenith);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        fb_bind(&shFB);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        pass_use(&shProjectPass);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, octaCaptureFB.color);
        pass_set_i32(&shProjectPass, "uOcta", 0);
        pass_set_f32(&shProjectPass, "uOctaSize", 128.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        std::cerr << "[SH] probe captured and projected\n";
    }

    // PROBE-BAKE: build a flat triangle list for the SG bake, then bake once.
    TriGrid triGrid;
    ProbeGrid probeGrid;
    {
        std::vector<float> triPos;
        std::vector<float> triAlb;

        auto emitTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                           const glm::vec3& alb) {
            triPos.push_back(a.x); triPos.push_back(a.y); triPos.push_back(a.z);
            triPos.push_back(b.x); triPos.push_back(b.y); triPos.push_back(b.z);
            triPos.push_back(c.x); triPos.push_back(c.y); triPos.push_back(c.z);
            triAlb.push_back(alb.x); triAlb.push_back(alb.y); triAlb.push_back(alb.z);
        };

        auto emitQuad = [&](const glm::vec3& p0, const glm::vec3& p1,
                            const glm::vec3& p2, const glm::vec3& p3,
                            const glm::vec3& alb) {
            emitTri(p0, p1, p2, alb);
            emitTri(p0, p2, p3, alb);
        };

        auto emitCube = [&](const glm::mat4& M, const glm::vec3& alb) {
            glm::vec3 v[8];
            int i = 0;
            for (int z = 0; z < 2; ++z)
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 2; ++x) {
                        glm::vec4 p = M * glm::vec4(x ? 0.5f : -0.5f,
                                                    y ? 0.5f : -0.5f,
                                                    z ? 0.5f : -0.5f, 1.f);
                        v[i++] = glm::vec3(p);
                    }
            // Faces: (indices of the 8 corners: 0..7 in x,y,z order)
            // front  z=+: 4,5,7,6
            // back   z-: 0,2,3,1
            // top    y=+: 2,3,7,6
            // bottom y-: 0,1,5,4
            // right  x=+: 1,3,7,5
            // left   x-: 0,4,6,2
            emitQuad(v[4], v[5], v[7], v[6], alb);
            emitQuad(v[0], v[2], v[3], v[1], alb);
            emitQuad(v[2], v[3], v[7], v[6], alb);
            emitQuad(v[0], v[1], v[5], v[4], alb);
            emitQuad(v[1], v[3], v[7], v[5], alb);
            emitQuad(v[0], v[4], v[6], v[2], alb);
        };

        // Ground plane
        {
            glm::vec3 mn(0, 0, 0), mx(map.W*map.tile, 0, map.H*map.tile);
            emitQuad(glm::vec3(mn.x, 0, mn.z),
                     glm::vec3(mx.x, 0, mn.z),
                     glm::vec3(mx.x, 0, mx.z),
                     glm::vec3(mn.x, 0, mx.z),
                     glm::vec3(0.35f, 0.45f, 0.25f));
        }

        // Walls and props (emitted as cubes at their OBB centers)
        for (auto& inst : scene) {
            if (inst.mesh == &cube) {
                emitCube(inst.model, inst.color);
            }
        }
        // Wait — cube instances were removed from scene. Use cubeModels instead.
        for (size_t i = 0; i < cubeModels.size(); ++i) {
            emitCube(cubeModels[i], cubeColors[i]);
        }

        // Helmet — approximate with its bounding box
        for (auto& gm : helmet) {
            // Its model matrix from the scene (find matching instance)
            // For simplicity, approximate the helmet with a cube at its position.
            glm::mat4 M = glm::translate(glm::mat4(1.f), glm::vec3(48.f, 2.f, 44.f))
                        * glm::scale(glm::mat4(1.f), glm::vec3(3.f, 3.f, 3.f));
            emitCube(M, glm::vec3(0.6f, 0.6f, 0.6f));
        }

        glm::vec3 gmin(0, -1, 0);
        glm::vec3 gmax(map.W*map.tile, 4, map.H*map.tile);
        triGrid = tri_grid_build(triPos, triAlb, (int)(triPos.size() / 9), gmin, gmax);

        glm::vec3 probe_min(0, 0.5f, 0);
        glm::vec3 probe_max(map.W*map.tile, 3.5f, map.H*map.tile);
        probeGrid = probe_grid_create(probe_min, probe_max);

        probe_grid_bake(&probeGrid, &triGrid,
                        gmin, gmax,
                        0.f, glm::vec3(0.35f, 0.45f, 0.25f),
                        glm::vec3(0.65f, 0.75f, 0.90f),
                        glm::vec3(0.15f, 0.35f, 0.75f),
                        lightDir, lightColor, lightIntensity);
    }

    while (!glfwWindowShouldClose(win)) {
        float now = (float)glfwGetTime();
        float dt = now - lastTime; lastTime = now;
        if (dt > 0.1f) dt = 0.1f;   // clamp big hitches

        // ---------- Input ----------
        cam.mouse(gInput.dx, gInput.dy);
        gInput.dx = gInput.dy = 0;

        glm::vec3 wish(0);
        if (gInput.w) wish += cam.forward();
        if (gInput.s) wish -= cam.forward();
        if (gInput.d) wish += cam.right();
        if (gInput.a) wish -= cam.right();
        wish.y = 0.f;
        if (glm::dot(wish, wish) > 1e-6f) {
            float spd = cam.speed * (gInput.shift ? 2.5f : 1.0f);
            wish = glm::normalize(wish) * spd;
        }
        vy -= 20.f * dt;
        if (vy < -30.f) vy = -30.f;
        wish.y = vy;
        player.move(wish * dt, world);
        if (player.grounded && vy < 0.f) vy = 0.f;
        cam.pos = player.eye();

        // ---------- Window size ----------
        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw <= 0 || fbh <= 0) { glfwPollEvents(); continue; }
        ensureFB(fbw, fbh);

        float aspect = (float)fbw / (float)fbh;
        glm::mat4 proj = glm::perspective(glm::radians(70.f), aspect, 0.1f, 200.f);
        {
            float jx, jy;
            taa_jitter(frameIndex, &jx, &jy);
            proj[2][0] += jx * 2.0f / (float)fbw;
            proj[2][1] += jy * 2.0f / (float)fbh;
        }
        glm::mat4 view = cam.view();
        glm::mat4 viewProj = proj * view;

        // ---------- Shadow pass (one per cascade) ----------
        shadow.update(view, proj, lightDir, aspect,
                      glm::radians(70.f), 0.1f);
        for (int c = 0; c < CSM_CASCADE_COUNT; ++c) {
            shadow.bindForWriting(c);
            depthInstShader.use();
            depthInstShader.set("uLightSpace", shadow.lightSpace[c]);
            instancedCube.draw();

            depthModelShader.use();
            depthModelShader.set("uLightSpace", shadow.lightSpace[c]);
            for (auto& inst : scene) {
                depthModelShader.set("uModel", inst.model);
                inst.mesh->draw();
            }
        }
        shadow.unbindForWriting();

        // ---------- SSAO pass ----------
        // Reads sceneFB.depth (last frame's depth — one-frame lag is fine for AO).
        {
            glm::mat4 invVP = glm::inverse(viewProj);

            fb_bind(&ssaoFB);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glClearColor(1,1,1,1);
            glClear(GL_COLOR_BUFFER_BIT);
            pass_use(&ssaoPass);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sceneFB.depth);
            pass_set_i32(&ssaoPass, "uSceneDepth", 0);
            pass_set_mat4(&ssaoPass, "uInvViewProj", invVP);
            pass_set_mat4(&ssaoPass, "uViewProj", viewProj);
            pass_set_vec3(&ssaoPass, "uCamPos", cam.pos);
            pass_set_vec2(&ssaoPass, "uResolution", glm::vec2(fbw/2, fbh/2));
            pass_set_f32(&ssaoPass, "uRadius", 0.5f);
            pass_set_f32(&ssaoPass, "uBias", 0.02f);
            pass_set_f32(&ssaoPass, "uIntensity", 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            fb_bind(&ssaoBlurFB);
            glClear(GL_COLOR_BUFFER_BIT);
            pass_use(&ssaoBlurPass);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ssaoFB.color);
            pass_set_i32(&ssaoBlurPass, "uSSAO", 0);
            pass_set_vec2(&ssaoBlurPass, "uInvResolution", glm::vec2(2.f/fbw, 2.f/fbh));
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        // ---------- SSGI pass ----------
        // Reads sceneFB.color (prev lit), sceneFB.depth (prev), gbuf.color1/2/3 (prev).
        {
            glm::mat4 invVP = glm::inverse(viewProj);

            fb_bind(&ssgiFB);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT);
            pass_use(&ssgiPass);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sceneFB.color);
            pass_set_i32(&ssgiPass, "uSceneColor", 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, sceneFB.depth);
            pass_set_i32(&ssgiPass, "uSceneDepth", 1);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gbuf.color1);   // albedo
            pass_set_i32(&ssgiPass, "uGBufferAlbedo", 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, gbuf.color2);   // normal+rough
            pass_set_i32(&ssgiPass, "uGBufferNormal", 3);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, gbuf.color3);   // emissive
            pass_set_i32(&ssgiPass, "uGBufferEmissive", 4);

            pass_set_mat4(&ssgiPass, "uInvViewProj", invVP);
            pass_set_mat4(&ssgiPass, "uViewProj", viewProj);
            pass_set_vec3(&ssgiPass, "uCamPos", cam.pos);
            pass_set_vec2(&ssgiPass, "uResolution", glm::vec2(fbw/2, fbh/2));
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        // ---------- Geometry pass -> G-buffer ----------
        fb_bind(&gbuf);
        {
            GLenum bufs[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                               GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
            glDrawBuffers(4, bufs);
        }
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Instanced cubes
        pbrInstShader.use();
        pbrInstShader.set("uViewProj",    viewProj);
        pbrInstShader.set("uView",        view);
        pbrInstShader.set("uPrevViewProj", prevViewProj);
        for (int i = 0; i < CSM_CASCADE_COUNT; ++i) {
            char n[32]; std::snprintf(n, 32, "uLightSpace[%d]", i);
            pbrInstShader.set(n, shadow.lightSpace[i]);
        }
        pbrInstShader.set("uCascadeSplits[0]", shadow.splits[0]);
        pbrInstShader.set("uCascadeSplits[1]", shadow.splits[1]);
        pbrInstShader.set("uCascadeSplits[2]", shadow.splits[2]);
        pbrInstShader.set("uCamPos",      cam.pos);
        pbrInstShader.set("uLightDir",    lightDir);
        pbrInstShader.set("uLightColor",  lightColor);
        pbrInstShader.set("uLightIntensity", lightIntensity);
        pbrInstShader.set("uShadowMap",   1);
        pbrInstShader.set("uShadowTexel", 1.0f / (float)shadow.size);
        pbrInstShader.set("uShadowEnabled", 1);
        pbrInstShader.set("uCrateTex",    2);
        pbrInstShader.set("uSSAO",        4);
        pbrInstShader.set("uSH",          5);
        pbrInstShader.set("uSSGI",        6);
        pbrInstShader.set("uProbeSGs",    12);
        pbrInstShader.set("uSceneGridMin", probeGrid.grid_min);
        pbrInstShader.set("uSceneGridMax", probeGrid.grid_max);
        glActiveTexture(GL_TEXTURE12);
        glBindTexture(GL_TEXTURE_2D, probeGrid.fb.color);
        glActiveTexture(GL_TEXTURE0);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, ssgiFB.color);
        glActiveTexture(GL_TEXTURE0);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, shFB.color);
        glActiveTexture(GL_TEXTURE0);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, ssaoBlurFB.color);
        glActiveTexture(GL_TEXTURE0);
        shadow.bindForReading(1);
        if (crateTex.id) crateTex.bind(2);
        instancedCube.upload(cubeModels, cubeColors, cubeMaterials);
        instancedCube.draw();

        // Non-instanced geometry (floor, GLB)
        pbrModelShader.use();
        pbrModelShader.set("uViewProj",    viewProj);
        pbrModelShader.set("uPrevViewProj", prevViewProj);
        for (int i = 0; i < CSM_CASCADE_COUNT; ++i) {
            char n[32]; std::snprintf(n, 32, "uLightSpace[%d]", i);
            pbrModelShader.set(n, shadow.lightSpace[i]);
        }
        pbrModelShader.set("uCascadeSplits[0]", shadow.splits[0]);
        pbrModelShader.set("uCascadeSplits[1]", shadow.splits[1]);
        pbrModelShader.set("uCascadeSplits[2]", shadow.splits[2]);
        pbrModelShader.set("uCamPos",      cam.pos);
        pbrModelShader.set("uLightDir",    lightDir);
        pbrModelShader.set("uLightColor",  lightColor);
        pbrModelShader.set("uLightIntensity", lightIntensity);
        pbrModelShader.set("uShadowMap",   1);
        pbrModelShader.set("uShadowTexel", 1.0f / (float)shadow.size);
        pbrModelShader.set("uShadowEnabled", 1);
        pbrModelShader.set("uCrateTex",    2);
        pbrModelShader.set("uSSAO",        4);
        pbrModelShader.set("uSH",          5);
        pbrModelShader.set("uSSGI",        6);
        pbrModelShader.set("uProbeSGs",    12);
        pbrModelShader.set("uSceneGridMin", probeGrid.grid_min);
        pbrModelShader.set("uSceneGridMax", probeGrid.grid_max);
        pbrModelShader.set("uGroundTex",   3);
        if (floorTex.id) floorTex.bind(3);
        shadow.bindForReading(1);
        if (crateTex.id) crateTex.bind(2);
        for (auto& inst : scene) {
            pbrModelShader.set("uModel", inst.model);
            pbrModelShader.set("uNormalMat", glm::mat3(glm::transpose(glm::inverse(inst.model))));
            pbrModelShader.set("uBaseColor", inst.color);
            pbrModelShader.set("uMaterialOverride", inst.material);
            pbrModelShader.set("uUVScale", inst.uvScale);

            if (inst.mat) {
                pbrModelShader.set("uIsPBR", 1);
                const Material& M = *inst.mat;
                pbrModelShader.set("uBaseColorFactor", M.baseColorFactor);
                pbrModelShader.set("uMetallicFactor", M.metallicFactor);
                pbrModelShader.set("uRoughnessFactor", M.roughnessFactor);
                pbrModelShader.set("uNormalScale",     M.normalScale);
                pbrModelShader.set("uEmissiveFactor", M.emissiveFactor);
                pbrModelShader.set("uHasBaseColor", M.basecolor ? 1 : 0);
                pbrModelShader.set("uHasMR",        M.metallicRoughness ? 1 : 0);
                pbrModelShader.set("uHasNormal",    M.normal ? 1 : 0);
                pbrModelShader.set("uHasEmissive",  M.emissive ? 1 : 0);
                pbrModelShader.set("uHasOcclusion", M.occlusion ? 1 : 0);
                pbrModelShader.set("uBaseColorTex",  7);
                pbrModelShader.set("uMRTex",         8);
                pbrModelShader.set("uNormalTex",     9);
                pbrModelShader.set("uEmissiveTex",   10);
                pbrModelShader.set("uOcclusionTex",  11);
                if (M.basecolor) {
                    glActiveTexture(GL_TEXTURE7);
                    glBindTexture(GL_TEXTURE_2D, M.basecolor);
                }
                if (M.metallicRoughness) {
                    glActiveTexture(GL_TEXTURE8);
                    glBindTexture(GL_TEXTURE_2D, M.metallicRoughness);
                }
                if (M.normal) {
                    glActiveTexture(GL_TEXTURE9);
                    glBindTexture(GL_TEXTURE_2D, M.normal);
                }
                if (M.emissive) {
                    glActiveTexture(GL_TEXTURE10);
                    glBindTexture(GL_TEXTURE_2D, M.emissive);
                }
                if (M.occlusion) {
                    glActiveTexture(GL_TEXTURE11);
                    glBindTexture(GL_TEXTURE_2D, M.occlusion);
                }
                glActiveTexture(GL_TEXTURE0);
            } else {
                pbrModelShader.set("uIsPBR", 0);
            }
            inst.mesh->draw();
        }
        fb_unbind(fbw, fbh);

        // ---------- Blit current lit color into sceneFB.color ----------
        // SSGI on the next frame reads this as its "previous frame" input.
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuf.fbo);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFB.fbo);
            GLenum b = GL_COLOR_ATTACHMENT0;
            glDrawBuffers(1, &b);
            glBlitFramebuffer(0, 0, gbuf.width, gbuf.height,
                              0, 0, sceneFB.width, sceneFB.height,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // ---------- SSR pass ----------
        {
            glm::mat4 invVP = glm::inverse(viewProj);

            fb_bind(&ssrFB);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glClearColor(0,0,0,0);
            glClear(GL_COLOR_BUFFER_BIT);
            pass_use(&ssrPass);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gbuf.color);
            pass_set_i32(&ssrPass, "uSceneColor", 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, sceneFB.depth);
            pass_set_i32(&ssrPass, "uSceneDepth", 1);
            pass_set_mat4(&ssrPass, "uViewProj", viewProj);
            pass_set_mat4(&ssrPass, "uInvViewProj", invVP);
            pass_set_vec3(&ssrPass, "uCamPos", cam.pos);
            pass_set_vec2(&ssrPass, "uResolution", glm::vec2(fbw, fbh));
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Composite SSR over the lit color
            fb_bind(&ssrCompositeFB);
            glClear(GL_COLOR_BUFFER_BIT);
            pass_use(&ssrCompositePass);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gbuf.color);
            pass_set_i32(&ssrCompositePass, "uSceneColor", 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, ssrFB.color);
            pass_set_i32(&ssrCompositePass, "uSSRColor", 1);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, sceneFB.depth);
            pass_set_i32(&ssrCompositePass, "uSceneDepth", 2);
            pass_set_mat4(&ssrCompositePass, "uInvViewProj", invVP);
            pass_set_vec3(&ssrCompositePass, "uCamPos", cam.pos);
            pass_set_vec2(&ssrCompositePass, "uResolution", glm::vec2(fbw, fbh));
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        // ---------- Volumetric fog ----------
        // Half-res raymarch, full-res composite. Runs after geometry so
        // it reads this frame's depth, and before TAA so the temporal
        // filter smooths out the raymarch noise.
        {
            glm::mat4 invVP = glm::inverse(viewProj);

            fb_bind(&fogFB);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT);
            pass_use(&fogRaymarchPass);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sceneFB.depth);
            pass_set_i32(&fogRaymarchPass, "uSceneDepth", 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D_ARRAY, shadow.depthTex);
            pass_set_i32(&fogRaymarchPass, "uShadowMap", 1);

            for (int i = 0; i < CSM_CASCADE_COUNT; ++i) {
                char n[32];
                std::snprintf(n, 32, "uLightSpace[%d]", i);
                pass_set_mat4(&fogRaymarchPass, n, shadow.lightSpace[i]);
                std::snprintf(n, 32, "uCascadeSplits[%d]", i);
                pass_set_f32(&fogRaymarchPass, n, shadow.splits[i]);
            }
            pass_set_f32(&fogRaymarchPass, "uShadowTexel", 1.0f / (float)shadow.size);

            pass_set_mat4(&fogRaymarchPass, "uInvViewProj", invVP);
            pass_set_vec3(&fogRaymarchPass, "uCamPos", cam.pos);
            pass_set_vec3(&fogRaymarchPass, "uLightDir", lightDir);
            pass_set_vec3(&fogRaymarchPass, "uLightColor", lightColor);
            pass_set_f32 (&fogRaymarchPass, "uLightIntensity", lightIntensity);
            pass_set_vec2(&fogRaymarchPass, "uFullResolution", glm::vec2(fbw, fbh));
            pass_set_vec2(&fogRaymarchPass, "uHalfResolution", glm::vec2(fbw/2, fbh/2));

            // Fog tuning — these are the knobs
            pass_set_f32(&fogRaymarchPass, "uFogDensity",     0.015f);
            pass_set_f32(&fogRaymarchPass, "uFogHeightFall",  0.20f);
            pass_set_f32(&fogRaymarchPass, "uFogMaxDist",     80.0f);
            pass_set_f32(&fogRaymarchPass, "uFogSteps",       32.0f);
            pass_set_f32(&fogRaymarchPass, "uSunScatter",     1.5f);
            pass_set_f32(&fogRaymarchPass, "uAmbientScatter", 1.5f);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Composite fog over scene
            fb_bind(&fogCompositeFB);
            glClear(GL_COLOR_BUFFER_BIT);
            pass_use(&fogCompositePass);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ssrCompositeFB.color);
            pass_set_i32(&fogCompositePass, "uSceneColor", 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, fogFB.color);
            pass_set_i32(&fogCompositePass, "uFogTexture", 1);
            pass_set_vec2(&fogCompositePass, "uInvResolution", glm::vec2(1.f/fbw, 1.f/fbh));
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        // ---------- TAA resolve ----------
        int curr = taaPing;
        int prev = 1 - taaPing;
        {
            fb_bind(&taaHistory[curr]);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT);

            pass_use(&taaPass);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gbuf.color);
            pass_set_i32(&taaPass, "uCurrent", 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, taaHistory[prev].color);
            pass_set_i32(&taaPass, "uHistory", 1);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gbuf.color1);   // albedo, prevUV.x in .a
            pass_set_i32(&taaPass, "uGBuffer", 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, gbuf.color3);   // emissive, prevUV.y in .a
            pass_set_i32(&taaPass, "uGBuffer2", 3);

            pass_set_vec2(&taaPass, "uResolution", glm::vec2(fbw, fbh));
            pass_set_f32(&taaPass, "uBlendFactor",
                         frameIndex == 0 ? 0.0f : 0.9f);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        // ---------- Tonemap G-buffer attachment 0 -> backbuffer ----------
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        pass_use(&tonemapPass);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, taaHistory[curr].color);
        pass_set_i32(&tonemapPass, "uScene", 0);
        pass_set_vec2(&tonemapPass, "uInvSize", glm::vec2(1.f/fbw, 1.f/fbh));
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        taaPing = 1 - taaPing;
        prevViewProj = viewProj;
        frameIndex++;
        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // Cleanup: destroy GL objects while the context is alive
    if (gbuf.fbo)    fb_destroy(&gbuf);
    fb_destroy(&octaCaptureFB);
    fb_destroy(&shFB);
    if (ssaoFB.fbo)  fb_destroy(&ssaoFB);
    if (ssgiFB.fbo)  fb_destroy(&ssgiFB);
    if (fogFB.fbo)   fb_destroy(&fogFB);
    if (fogCompositeFB.fbo) fb_destroy(&fogCompositeFB);
    if (ssaoBlurFB.fbo) fb_destroy(&ssaoBlurFB);
    if (sceneFB.fbo) fb_destroy(&sceneFB);
    pass_destroy(&tonemapPass);
    tri_grid_destroy(&triGrid);
    probe_grid_destroy(&probeGrid);
    if (ssrFB.fbo) fb_destroy(&ssrFB);
    if (ssrCompositeFB.fbo) fb_destroy(&ssrCompositeFB);

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
