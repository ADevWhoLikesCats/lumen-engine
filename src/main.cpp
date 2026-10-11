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
#include "mesh.h"
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
    };
    std::vector<Instance> scene;

    auto add = [&](const Mesh& m, glm::mat4 xf, glm::vec3 c,
                   const Texture* t = nullptr, glm::vec2 uvs = {1,1}, float mat = 0.f) {
        scene.push_back({&m, xf, c, t, uvs, mat});
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

    // ---------- Per-frame state ----------
    Camera cam;
    CharacterController player;
    player.pos = map.spawn;
    player.height = 1.7f;
    player.radius = 0.3f;
    float vy = 0.f;

    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    glm::vec3 lightColor(1.0f, 0.96f, 0.90f);
    float     lightIntensity = 1.8f;

    Framebuffer sceneFB;   // HDR lit scene with a depth texture
    Framebuffer ssaoFB;    // R8 AO, raw
    Framebuffer ssaoBlurFB;// R8 AO, blurred
    Framebuffer gbuf;      // G-buffer (4 MRT attachments, borrowed depth)
    int lastW = 0, lastH = 0;

    auto ensureFB = [&](int w, int h) {
        if (w == lastW && h == lastH) return;
        lastW = w; lastH = h;
        if (sceneFB.fbo) fb_destroy(&sceneFB);
        if (gbuf.fbo)    fb_destroy(&gbuf);
    if (ssaoFB.fbo)  fb_destroy(&ssaoFB);
    if (ssaoBlurFB.fbo) fb_destroy(&ssaoBlurFB);
        sceneFB     = fb_create_with_depth_tex(w, h, FB_RGBA16F);
        gbuf        = fb_create_gbuffer(w, h, sceneFB.depth);
        ssaoFB      = fb_create(w, h, FB_R8);
        ssaoBlurFB  = fb_create(w, h, FB_R8);
    };

    float lastTime = (float)glfwGetTime();
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
            pass_set_vec2(&ssaoPass, "uResolution", glm::vec2(fbw, fbh));
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
            pass_set_vec2(&ssaoBlurPass, "uInvResolution", glm::vec2(1.f/fbw, 1.f/fbh));
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
        pbrModelShader.set("uView",        view);
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
            inst.mesh->draw();
        }
        fb_unbind(fbw, fbh);

        // ---------- Tonemap G-buffer attachment 0 -> backbuffer ----------
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        pass_use(&tonemapPass);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gbuf.color);
        pass_set_i32(&tonemapPass, "uScene", 0);
        pass_set_vec2(&tonemapPass, "uInvSize", glm::vec2(1.f/fbw, 1.f/fbh));
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // Cleanup: destroy GL objects while the context is alive
    if (gbuf.fbo)    fb_destroy(&gbuf);
    if (ssaoFB.fbo)  fb_destroy(&ssaoFB);
    if (ssaoBlurFB.fbo) fb_destroy(&ssaoBlurFB);
    if (sceneFB.fbo) fb_destroy(&sceneFB);
    pass_destroy(&tonemapPass);

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
