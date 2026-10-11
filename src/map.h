#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include "collider.h"

struct MapCell { bool walkable = false; };

struct GeneratedMap {
    int W = 0, H = 0;
    float tile = 4.0f;
    std::vector<MapCell> cells;          // W*H, row-major
    glm::vec3 spawn{0};
    std::vector<glm::mat4> wallTransforms;   // unit cube -> world
    std::vector<OBB>       wallColliders;
    glm::mat4              floorTransform;   // unit quad, scaled

    bool walkable(int x, int y) const {
        if (x < 0 || y < 0 || x >= W || y >= H) return false;
        return cells[y*W + x].walkable;
    }
    glm::vec3 cellCenter(int x, int y) const {
        return glm::vec3((x + 0.5f) * tile, 0.f, (y + 0.5f) * tile);
    }
};

// Generate a room-and-corridor layout. Deterministic for a given seed.
GeneratedMap generateMap(int W, int H, float tile, uint32_t seed);
