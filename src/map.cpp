#include "map.h"
#include <random>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

GeneratedMap generateMap(int W, int H, float tile, uint32_t seed) {
    GeneratedMap m;
    m.W = W; m.H = H; m.tile = tile;
    m.cells.assign(W*H, MapCell{false});

    std::mt19937 rng(seed);

    // --- Carve rooms ---
    int roomCount = 6 + (rng() % 5);
    std::vector<glm::ivec4> rooms;   // x, y, w, h
    for (int i = 0; i < roomCount; ++i) {
        int rw = 3 + rng() % 5;
        int rh = 3 + rng() % 5;
        int rx = 1 + rng() % std::max(1, W - rw - 2);
        int ry = 1 + rng() % std::max(1, H - rh - 2);
        rooms.push_back({rx, ry, rw, rh});
        for (int y = ry; y < ry+rh; ++y)
            for (int x = rx; x < rx+rw; ++x)
                m.cells[y*W + x].walkable = true;
    }

    // --- Connect rooms with L-shaped corridors ---
    auto carveH = [&](int x0, int x1, int y) {
        if (x0 > x1) std::swap(x0, x1);
        for (int x = x0; x <= x1; ++x)
            if (x >= 0 && x < W && y >= 0 && y < H)
                m.cells[y*W + x].walkable = true;
    };
    auto carveV = [&](int y0, int y1, int x) {
        if (y0 > y1) std::swap(y0, y1);
        for (int y = y0; y <= y1; ++y)
            if (x >= 0 && x < W && y >= 0 && y < H)
                m.cells[y*W + x].walkable = true;
    };
    for (size_t i = 1; i < rooms.size(); ++i) {
        auto& a = rooms[i-1];
        auto& b = rooms[i];
        int ax = a.x + a.z/2, ay = a.y + a.w/2;
        int bx = b.x + b.z/2, by = b.y + b.w/2;
        if (rng() & 1) { carveH(ax, bx, ay); carveV(ay, by, bx); }
        else           { carveV(ay, by, ax); carveH(ax, bx, bx); }
    }

    // --- Guarantee a spawn cell ---
    for (size_t i = 0; i < rooms.size(); ++i) {
        auto& r = rooms[i];
        int cx = r.x + r.z/2;
        int cy = r.y + r.w/2;
        if (m.walkable(cx, cy)) {
            m.spawn = m.cellCenter(cx, cy) + glm::vec3(0, 1.0f, 0);
            break;
        }
    }
    if (m.spawn == glm::vec3(0)) {
        for (int y = 0; y < H && m.spawn == glm::vec3(0); ++y)
            for (int x = 0; x < W; ++x)
                if (m.walkable(x, y)) { m.spawn = m.cellCenter(x, y) + glm::vec3(0,1,0); break; }
    }

    // --- Build wall instances for non-walkable cells adjacent to walkable ones ---
    glm::mat4 unitCubeScale = glm::scale(glm::mat4(1.f), glm::vec3(tile, 3.0f, tile));
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (m.walkable(x, y)) continue;

            // Only emit a wall if a neighbor is walkable (interior walls).
            bool neighborWalkable =
                m.walkable(x-1,y) || m.walkable(x+1,y) ||
                m.walkable(x,y-1) || m.walkable(x,y+1);
            if (!neighborWalkable) continue;

            glm::vec3 c = m.cellCenter(x, y);
            glm::mat4 M = glm::translate(glm::mat4(1.f), c + glm::vec3(0, 1.5f, 0))
                        * unitCubeScale;
            m.wallTransforms.push_back(M);

            // Collider — half extents are (tile/2, 1.5, tile/2).
            m.wallColliders.push_back(
                OBB::fromMatrix(M, glm::vec3(0.5f, 0.5f, 0.5f)));
        }
    }

    // --- Floor: single quad, tiled UVs handled by the caller's shader ---
    float worldW = W * tile, worldH = H * tile;
    m.floorTransform =
        glm::translate(glm::mat4(1.f), glm::vec3(worldW*0.5f, 0.f, worldH*0.5f))
      * glm::scale(glm::mat4(1.f), glm::vec3(worldW, 1.f, worldH));

    return m;
}
