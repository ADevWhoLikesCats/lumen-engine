#pragma once
#include "mesh.h"
#include <vector>
#include <string>

struct GLBMesh {
    Mesh     mesh;
    Material material;
};

// Load a .glb, returning each primitive's geometry + material.
std::vector<GLBMesh> loadGLB(const std::string& path);
