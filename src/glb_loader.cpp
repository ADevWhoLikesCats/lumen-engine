#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include "glb_loader.h"

#include <iostream>

using namespace glm;

static vec3 readVec3(const float* p) { return {p[0], p[1], p[2]}; }
static vec2 readVec2(const float* p) { return {p[0], p[1]}; }

std::vector<Mesh> loadGLB(const std::string& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if (!warn.empty()) std::cerr << "glTF warn: " << warn << "\n";
    if (!ok) { std::cerr << "glTF error: " << err << "\n"; return {}; }

    std::vector<Mesh> meshes;

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            auto itPos = prim.attributes.find("POSITION");
            if (itPos == prim.attributes.end()) continue;

            const auto& accPos = model.accessors[itPos->second];
            const auto& bvPos  = model.bufferViews[accPos.bufferView];
            const auto& bufPos = model.buffers[bvPos.buffer];
            const float* pos = reinterpret_cast<const float*>(
                bufPos.data.data() + bvPos.byteOffset + accPos.byteOffset);
            size_t stride = bvPos.byteStride ? bvPos.byteStride / sizeof(float) : 3;

            std::vector<vec3> positions;
            positions.reserve(accPos.count);
            for (size_t i = 0; i < accPos.count; ++i)
                positions.push_back(readVec3(pos + i * stride));

            std::vector<vec3> normals;
            auto itNrm = prim.attributes.find("NORMAL");
            if (itNrm != prim.attributes.end()) {
                const auto& acc = model.accessors[itNrm->second];
                const auto& bv  = model.bufferViews[acc.bufferView];
                const auto& buf = model.buffers[bv.buffer];
                const float* n = reinterpret_cast<const float*>(
                    buf.data.data() + bv.byteOffset + acc.byteOffset);
                size_t s = bv.byteStride ? bv.byteStride / sizeof(float) : 3;
                normals.reserve(acc.count);
                for (size_t i = 0; i < acc.count; ++i)
                    normals.push_back(readVec3(n + i * s));
            }

            std::vector<vec2> uvs;
            auto itUv = prim.attributes.find("TEXCOORD_0");
            if (itUv != prim.attributes.end()) {
                const auto& acc = model.accessors[itUv->second];
                const auto& bv  = model.bufferViews[acc.bufferView];
                const auto& buf = model.buffers[bv.buffer];
                const float* u = reinterpret_cast<const float*>(
                    buf.data.data() + bv.byteOffset + acc.byteOffset);
                size_t s = bv.byteStride ? bv.byteStride / sizeof(float) : 2;
                uvs.reserve(acc.count);
                for (size_t i = 0; i < acc.count; ++i)
                    uvs.push_back(readVec2(u + i * s));
            }

            std::vector<uint32_t> indices;
            if (prim.indices >= 0) {
                const auto& acc = model.accessors[prim.indices];
                const auto& bv  = model.bufferViews[acc.bufferView];
                const auto& buf = model.buffers[bv.buffer];
                const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
                indices.reserve(acc.count);
                for (size_t i = 0; i < acc.count; ++i) {
                    switch (acc.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  indices.push_back(base[i]); break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: indices.push_back(reinterpret_cast<const uint16_t*>(base)[i]); break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   indices.push_back(reinterpret_cast<const uint32_t*>(base)[i]); break;
                    }
                }
            } else {
                for (uint32_t i = 0; i < positions.size(); ++i) indices.push_back(i);
            }

            std::vector<Vertex> verts;
            verts.reserve(positions.size());
            for (size_t i = 0; i < positions.size(); ++i) {
                Vertex v;
                v.pos = positions[i];
                v.nrm = i < normals.size() ? normals[i] : vec3(0,1,0);
                v.uv  = i < uvs.size()     ? uvs[i]     : vec2(0);
                verts.push_back(v);
            }

            meshes.emplace_back(verts, indices);
        }
    }
    return meshes;
}
