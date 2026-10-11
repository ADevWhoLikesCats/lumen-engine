// ---------------------------------------------------------------
// Minimal forward-rendered FPS engine with Lambert lighting.
// OpenGL 3.3 core. GLFW for window/input, GLM for math, GLAD for loader.
// ---------------------------------------------------------------
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "collider.h"
#include "controller.h"
#include "mesh.h"
#include "instanced.h"
#include "shadowmap.h"
#include "texture.h"
#include "map.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <random>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

// ===============================================================
// Shader helper
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

struct Shader {
    GLuint id = 0;
    mutable std::unordered_map<std::string, GLint> cache;

    // Look up once, cache forever.
    GLint loc(const char* n) const {
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        GLint l = glGetUniformLocation(id, n);
        cache.emplace(n, l);
        return l;
    }

    Shader(const char* vsPath, const char* fsPath) {
        std::string vsrc = readFile(vsPath);
        GLuint vs = compile(GL_VERTEX_SHADER, vsrc);
        std::string fsrc = readFile(fsPath);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
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

    void set(const char* n, float v)              const { glUniform1f(loc(n), v); }
    void set(const char* n, int v)                const { glUniform1i(loc(n), v); }
    void set(const char* n, const glm::vec2& v)   const { glUniform2fv(loc(n),1,glm::value_ptr(v)); }
    void set(const char* n, const glm::vec3& v)   const { glUniform3fv(loc(n),1,glm::value_ptr(v)); }
    void set(const char* n, const glm::mat3& m)   const { glUniformMatrix3fv(loc(n),1,GL_FALSE,glm::value_ptr(m)); }
    void set(const char* n, const glm::mat4& m)   const { glUniformMatrix4fv(loc(n),1,GL_FALSE,glm::value_ptr(m)); }
};

// ===============================================================
// Mesh: position(3) normal(3) uv(2)
// ===============================================================
// --- Primitive builders ---------------------------------------

static Mesh makeCube() {
    Vertex v[24];
    glm::vec3 n[6] = {{ 0,0, 1},{ 0,0,-1},{ 1,0,0},{-1,0,0},{ 0,1,0},{ 0,-1,0}};
    glm::vec2 uvs[4] = {{0,0},{1,0},{1,1},{0,1}};
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        glm::vec3 a = n[f];
        glm::vec3 t = (fabsf(a.y) > 0.5f) ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
        glm::vec3 b = glm::cross(a, t);
        glm::vec3 c = glm::cross(a, b);
        // corner order: (-,-), (+,-), (+,+), (-,+)
        glm::vec3 p[4] = { -b-c, b-c, b+c, -b+c };
        for (int i = 0; i < 4; ++i) {
            v[vi++] = { p[i]*0.5f + a*0.5f, a, uvs[i] };
        }
    }
    std::vector<Vertex> verts(v, v+24);
    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; ++f) {
        uint32_t o = f*4;
        idx.insert(idx.end(), {o,o+1,o+2, o,o+2,o+3});
    }
    return Mesh(verts, idx);
}

static Mesh makePlane(float size) {
    float h = size * 0.5f;
    std::vector<Vertex> v = {
        {{-h,0,-h},{0,1,0},{0,0}},
        {{ h,0,-h},{0,1,0},{1,0}},
        {{ h,0, h},{0,1,0},{1,1}},
        {{-h,0, h},{0,1,0},{0,1}},
    };
    std::vector<uint32_t> i = {0,2,1, 0,3,2};
    return Mesh(v, i);
}

// ===============================================================
// Camera (FPS)
// ===============================================================
struct Camera {
    glm::vec3 pos{0, 1.7f, 5};
    float yaw = -90.f, pitch = 0.f;
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
        yaw   += dx * sens;
        pitch  = glm::clamp(pitch - dy * sens, -89.f, 89.f);
    }
    void move(float dt, bool w, bool s, bool a, bool d) {
        glm::vec3 dir{0};
        if (w) dir += forward();
        if (s) dir -= forward();
        if (d) dir += right();
        if (a) dir -= right();
        dir.y = 0;                       // walking, not flying
        if (glm::length(dir) > 0.0001f) pos += glm::normalize(dir) * speed * dt;
    }
    glm::mat4 view() const { return glm::lookAt(pos, pos + forward(), {0,1,0}); }
};

// ===============================================================
// Input state
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
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(glfwGetCurrentContext(), true);
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
// main
// ===============================================================
int main() {
    if (!glfwInit()) { std::cerr << "glfwInit failed\n"; return 1; }

    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    // glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* win = glfwCreateWindow(1280, 720, "Lambert FPS", nullptr, nullptr);
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

    // Render state
    glEnable(GL_DEPTH_TEST);
    // glEnable(GL_MULTISAMPLE);
glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Resources
    Shader lambert("shaders/lambert.vert", "shaders/lambert.frag");
    Mesh cube  = makeCube();
    Mesh floor = makePlane(1.f);

    Camera cam;
    CharacterController player;
    player.height  = 1.7f;
    player.radius  = 0.3f;
    float vy = 0.f;

    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

    // Scene: list of (mesh, model, color)
    struct Instance { const Mesh* mesh; glm::mat4 model; glm::vec3 color; const Texture* tex = nullptr; glm::vec2 uvScale{1,1}; float material = 0.f; };
    std::vector<Instance> scene;
    auto add = [&](const Mesh& m, glm::mat4 xf, glm::vec3 c, const Texture* t = nullptr,
                   glm::vec2 uvs = {1,1}, float mat = 0.f) {
        scene.push_back({&m, xf, c, t, uvs, mat});
    };

    std::vector<OBB> world;

    // ---- Generated map ----
    GeneratedMap map = generateMap(24, 24, 4.0f, /*seed=*/1337u);

    // Floor: one big quad, tiled via uUVScale.
    float floorTiles = (float)map.W;   // one tile per world cell
    Texture floorTex("assets/ground.png");
    Texture crateTex("assets/crate.png");
    add(floor,
        glm::translate(glm::mat4(1.f), glm::vec3(map.W*map.tile*0.5f, 0.f, map.H*map.tile*0.5f))
      * glm::scale(glm::mat4(1.f), glm::vec3(map.W*map.tile, 1.f, map.H*map.tile)),
        {1,1,1}, &floorTex, glm::vec2(floorTiles, floorTiles));

    // Floor collider (thin slab).
    {
        // Center the slab under the visual floor mesh, which sits at
        // (W*tile/2, 0, H*tile/2). Same translate as the mesh, minus half
        // the slab thickness so the top face is at y = 0.
        glm::vec3 ctr(map.W*map.tile*0.5f, -0.5f, map.H*map.tile*0.5f);
        glm::mat4 fm = glm::translate(glm::mat4(1.f), ctr)
                     * glm::scale(glm::mat4(1.f),
                                  glm::vec3(map.W*map.tile, 1.f, map.H*map.tile));
        world.push_back(OBB::fromMatrix(fm, glm::vec3(0.5f)));
    }

    // Walls — instanced from the generator.
    for (size_t i = 0; i < map.wallTransforms.size(); ++i) {
        add(cube, map.wallTransforms[i], {0.55f, 0.53f, 0.50f});
        world.push_back(map.wallColliders[i]);
    }

    // PROPS-BLOCK: scatter small objects across walkable cells.
    {
        std::mt19937 propRng(1337u ^ 0x9E3779B9u);   // deterministic per seed
        std::uniform_real_distribution<float> chance(0.f, 1.f);
        std::uniform_int_distribution<int>   kind(0, 2);
        std::uniform_real_distribution<float> jitter(-0.15f, 0.15f);

        int placed = 0;
        for (int y = 0; y < map.H; ++y) {
            for (int x = 0; x < map.W; ++x) {
                if (!map.walkable(x, y)) continue;
                if (chance(propRng) > 0.15f) continue;   // ~15% of open cells

                glm::vec3 c = map.cellCenter(x, y);
                // Skip the spawn cell so the player doesn't start inside a crate.
                if (glm::distance(c, map.spawn) < 2.0f) continue;

                // Jitter a bit so props don't sit perfectly on the grid.
                c.x += jitter(propRng);
                c.z += jitter(propRng);

                glm::vec3 half, color;
                float mat = 0.f;
                switch (kind(propRng)) {
                    case 0:  // crate
                        half  = {0.35f, 0.35f, 0.35f};
                        color = {0.62f, 0.42f, 0.24f};
                        mat   = 1.f;   // use crate texture
                        break;
                    case 1:  // pillar
                        half  = {0.25f, 1.00f, 0.25f};
                        color = {0.55f, 0.52f, 0.48f};   // grey stone
                        break;
                    default: // barrel
                        half  = {0.35f, 0.50f, 0.35f};
                        color = {0.32f, 0.38f, 0.42f};   // blue-grey metal
                        break;
                }

                glm::vec3 pos = c + glm::vec3(0, half.y, 0);   // sit on floor
                glm::mat4 m = glm::translate(glm::mat4(1.f), pos)
                            * glm::scale(glm::mat4(1.f), half * 2.f);
                add(cube, m, color, nullptr, glm::vec2(1,1), mat);

                // Collider: unit-cube OBB, scaled to the same half-extents.
                world.push_back(OBB::fromMatrix(m, glm::vec3(0.5f)));
                ++placed;
            }
        }
        std::cerr << "PROPS: placed " << placed << " objects\n";
    }

    // Move player spawn to a guaranteed-open cell.
    player.pos = map.spawn;

    auto box = [&](glm::vec3 p, glm::vec3 size, glm::vec3 color) {
        glm::mat4 m = glm::translate(glm::mat4(1.f), p) * glm::scale(glm::mat4(1.f), size);
        add(cube, m, color);
        world.push_back(OBB::fromMatrix(m, glm::vec3(0.5f)));
    };
    box({ 0, 0.5f,  0}, {1,1,1},   {0.85f, 0.32f, 0.28f});
    box({ 3, 0.75f,-2}, {1.5f,1.5f,1.5f}, {0.30f, 0.62f, 0.85f});
    box({-3, 1.0f, -3}, {2,2,2},   {0.75f, 0.75f, 0.35f});
    box({-1, 0.3f,  4}, {0.6f,0.6f,3.f}, {0.4f, 0.7f, 0.4f});
    box({ 5, 0.25f, 3}, {2.f,0.5f,2.f},  {0.6f, 0.4f, 0.7f});

    {
        glm::mat4 m = glm::translate(glm::mat4(1.f), glm::vec3(-4.f, 0.8f, 2.f))
                    * glm::rotate(glm::mat4(1.f), glm::radians(28.f), glm::vec3(0,0,1))
                    * glm::scale(glm::mat4(1.f), glm::vec3(3.f, 0.4f, 2.f));
        add(cube, m, {0.5f, 0.5f, 0.55f});
        world.push_back(OBB::fromMatrix(m, glm::vec3(0.5f)));
    }

    float last = (float)glfwGetTime();

    // INSTANCED-SETUP: gather every cube-drawn instance into one buffer.
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
    std::cerr << "INSTANCED: " << cubeModels.size() << " cube instances\n";

    InstancedMesh instancedCube;
    instancedCube.init(cube, cubeModels.size() + 16);

    Shader instShader("shaders/lambert_instanced.vert",
                      "shaders/lambert_instanced.frag");

    // Remove cube instances from the per-frame list; they'll draw instanced.
    scene.erase(std::remove_if(scene.begin(), scene.end(),
        [&](const Instance& i){ return i.mesh == &cube; }), scene.end());

    // SHADOW-SETUP
    ShadowMap shadow;
    shadow.init(60.0f);
    Shader depthInstShader ("shaders/depth_instanced.vert", "shaders/depth.frag");
    Shader depthModelShader("shaders/depth_model.vert",     "shaders/depth.frag");

    // SKY-SETUP
    Shader skyShader("shaders/sky.vert", "shaders/sky.frag");
    GLuint skyVAO = 0;
    glGenVertexArrays(1, &skyVAO);   // empty VAO, needed for core profile

    // Sky/fog colors (linear, before gamma).
    glm::vec3 skyHorizon(0.65f, 0.75f, 0.90f);
    glm::vec3 skyZenith (0.15f, 0.35f, 0.75f);
    float     fogDensity = 0.012f;

    while (!glfwWindowShouldClose(win)) {
        float now = (float)glfwGetTime();
        float dt = now - last; last = now;

        // --- input ---
        cam.mouse(gInput.dx, gInput.dy);
        gInput.dx = gInput.dy = 0;

        glm::vec3 wish(0);
        if (gInput.w) wish += cam.forward();
        if (gInput.s) wish -= cam.forward();
        if (gInput.d) wish += cam.right();
        if (gInput.a) wish -= cam.right();
        wish.y = 0.f;
        if (glm::dot(wish,wish) > 1e-6f) wish = glm::normalize(wish) * cam.speed;

        vy -= 20.f * dt;
        if (vy < -30.f) vy = -30.f;
        wish.y = vy;

        player.move(wish * dt, world);
        if (player.grounded && vy < 0.f) vy = 0.f;

        cam.pos = player.eye();

        // --- render ---
        int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.65f, 0.75f, 0.90f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = fbw > 0 ? (float)fbw / (float)fbh : 1.f;
        glm::mat4 proj = glm::perspective(glm::radians(70.f), aspect, 0.1f, 200.f);

        // SHADOW-PASS: render depth from the light's POV.
        shadow.bindForWriting();
        {
            depthInstShader.use();
            depthInstShader.set("uLightSpace", shadow.matrix());
            instancedCube.draw();

            depthModelShader.use();
            depthModelShader.set("uLightSpace", shadow.matrix());
            for (auto& inst : scene) {
                depthModelShader.set("uModel", inst.model);
                inst.mesh->draw();
            }
        }
        shadow.unbindForWriting();
        glViewport(0, 0, fbw, fbh);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // SKY-DRAW: full-screen triangle at the far plane.
        {
            glDepthMask(GL_FALSE);          // don't write depth
            glDisable(GL_CULL_FACE);        // triangle winds either way
            skyShader.use();
            glm::mat4 invVP = glm::inverse(proj * cam.view());
            skyShader.set("uInvViewProj",  invVP);
            skyShader.set("uHorizonColor", skyHorizon);
            skyShader.set("uZenithColor",  skyZenith);
            skyShader.set("uCameraPos",    cam.pos);
            glBindVertexArray(skyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE);
        }

        lambert.use();
        lambert.set("uLightSpace",  shadow.matrix());
        lambert.set("uShadowMap",   1);
        lambert.set("uShadowTexel", 1.0f / (float)shadow.size);
        lambert.set("uFogHorizon", skyHorizon);
        lambert.set("uFogZenith",  skyZenith);
        lambert.set("uFogDensity", fogDensity);
        shadow.bindForReading(1);
        lambert.set("uView",       cam.view());
        lambert.set("uProj",       proj);
        lambert.set("uCameraPos",  cam.pos);
        lambert.set("uLightDir",   lightDir);
        lambert.set("uLightColor", glm::vec3(1.0f, 0.96f, 0.90f));
        lambert.set("uAmbient",    glm::vec3(0.15f, 0.16f, 0.20f));
        lambert.set("uUseTexture", 0);

        { static int once = 0; if (once++ < 1) {
            std::cerr << "FLOOR-DIAG: scene[0].model[3] = "
                      << scene[0].model[3][0] << ","
                      << scene[0].model[3][1] << ","
                      << scene[0].model[3][2] << "\n";
            std::cerr << "FLOOR-DIAG: cam.pos = "
                      << cam.pos.x << "," << cam.pos.y << "," << cam.pos.z << "\n";
            std::cerr << "FLOOR-DIAG: cam.forward = "
                      << cam.forward().x << "," << cam.forward().y << "," << cam.forward().z << "\n";
          } }
        for (auto& inst : scene) {
            lambert.set("uModel", inst.model);
            lambert.set("uNormalMat", glm::mat3(glm::transpose(glm::inverse(inst.model))));
            lambert.set("uBaseColor", inst.color);
            lambert.set("uUVScale",   inst.uvScale);
            if (inst.tex && inst.tex->id) {
                lambert.set("uUseTexture", 1);
                lambert.set("uAlbedo", 0);
                inst.tex->bind(0);
            } else {
                lambert.set("uUseTexture", 0);
            }
            inst.mesh->draw();
        }

        // INSTANCED-DRAW: one draw call for all cubes.
        {
            instShader.use();
            instShader.set("uView",       cam.view());
            instShader.set("uProj",       proj);
            instShader.set("uCameraPos",  cam.pos);
            instShader.set("uLightDir",   lightDir);
            instShader.set("uLightColor", glm::vec3(1.0f, 0.96f, 0.90f));
            instShader.set("uAmbient",    glm::vec3(0.15f, 0.16f, 0.20f));
            instShader.set("uUseTexture", 0);
            instShader.set("uUVScale",    glm::vec2(1,1));
            instShader.set("uCrateTex",   2);
            if (crateTex.id) crateTex.bind(2);
            instShader.set("uLightSpace",  shadow.matrix());
            instShader.set("uShadowMap",   1);
            instShader.set("uShadowTexel", 1.0f / (float)shadow.size);
            instShader.set("uCameraPos",   cam.pos);
            instShader.set("uFogHorizon",  skyHorizon);
            instShader.set("uFogZenith",   skyZenith);
            instShader.set("uFogDensity",  fogDensity);
            shadow.bindForReading(1);
            instancedCube.upload(cubeModels, cubeColors, cubeMaterials);
            instancedCube.draw();
        }

        { // FPS-COUNT
            static int frames = 0;
            static double t0 = glfwGetTime();
            frames++;
            double now = glfwGetTime();
            if (now - t0 >= 1.0) {
                std::cerr << "FPS: " << (frames / (now - t0)) << "\n";
                frames = 0; t0 = now;
            }
        }
        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwDestroyWindow(win);
    floorTex.id = 0;   // GL context is dead; don't call glDeleteTextures
    glfwTerminate();
    return 0;
}
