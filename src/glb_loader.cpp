#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include "glb_loader.h"

#include <cmath>
#include <iostream>
#include <unordered_map>

using namespace glm;


// Compute per-vertex tangents from positions + UVs. Standard technique:
// for each triangle, build the TBN matrix from the UV gradient, accumulate
// per vertex, then orthonormalize against the vertex normal.
static void computeTangents(std::vector<Vertex>& verts,
                            const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> tanAccum(verts.size(), glm::vec3(0));
    std::vector<glm::vec3> bitAccum(verts.size(), glm::vec3(0));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
        const Vertex& v0 = verts[i0];
        const Vertex& v1 = verts[i1];
        const Vertex& v2 = verts[i2];

        glm::vec3 e1 = v1.pos - v0.pos;
        glm::vec3 e2 = v2.pos - v0.pos;
        glm::vec2 d1 = v1.uv  - v0.uv;
        glm::vec2 d2 = v2.uv  - v0.uv;

        float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-8f) continue;
        float r = 1.0f / det;

        glm::vec3 T = (e1 * d2.y - e2 * d1.y) * r;
        glm::vec3 B = (e2 * d1.x - e1 * d2.x) * r;

        tanAccum[i0] += T; tanAccum[i1] += T; tanAccum[i2] += T;
        bitAccum[i0] += B; bitAccum[i1] += B; bitAccum[i2] += B;
    }

    for (size_t i = 0; i < verts.size(); ++i) {
        glm::vec3 n = verts[i].nrm;
        glm::vec3 t = tanAccum[i];
        if (glm::dot(t, t) < 1e-8f) {
            verts[i].tangent = glm::vec4(0,0,0,1);
            continue;
        }
        // Gram-Schmidt orthogonalize
        t = glm::normalize(t - n * glm::dot(n, t));
        // Handedness: sign of dot(cross(N, T), B)
        float w = (glm::dot(glm::cross(n, t), bitAccum[i]) < 0.f) ? -1.f : 1.f;
        verts[i].tangent = glm::vec4(t, w);
    }
}

static vec3 readVec3(const float* p) { return {p[0], p[1], p[2]}; }
static vec2 readVec2(const float* p) { return {p[0], p[1]}; }

// Convert an image stored in the glTF buffer to a GL texture.
// srgb=1 for basecolor/emissive, 0 for normal/mr/occlusion.
static GLuint loadImageTexture(const tinygltf::Model& model, int imageIdx, int srgb) {
    if (imageIdx < 0 || imageIdx >= (int)model.images.size()) return 0;
    const auto& img = model.images[imageIdx];

    // tinygltf decodes images during LoadBinaryFromFile: img.image holds the
    // raw pixel data, img.width/img.height the dimensions, img.component the
    // number of channels. We upload it directly to a GL texture.
    if (img.image.empty() || img.width <= 0 || img.height <= 0) {
        std::cerr << "  image " << imageIdx << " has no data\n";
        return 0;
    }

    std::cerr << "IMG-DBG: idx=" << imageIdx
              << " w=" << img.width
              << " h=" << img.height
              << " comp=" << img.component
              << " bitdepth=" << img.bits
              << " bytes=" << img.image.size() << "\n";

    // Component count -> GL format
    GLenum fmt = GL_RGBA;
    if (img.component == 1) fmt = GL_RED;
    else if (img.component == 2) fmt = GL_RG;
    else if (img.component == 3) fmt = GL_RGB;
    else if (img.component == 4) fmt = GL_RGBA;
    else {
        std::cerr << "  unsupported component count " << img.component << "\n";
        return 0;
    }

    // Component type: 8-bit vs 16-bit vs float
    GLenum type = GL_UNSIGNED_BYTE;
    if (img.bits == 16) type = GL_UNSIGNED_SHORT;
    else if (img.bits == 32) type = GL_FLOAT;

    GLenum internal;
    if (type == GL_UNSIGNED_BYTE) {
        internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    } else {
        internal = GL_RGBA16F;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal,
                 img.width, img.height, 0, fmt, type, img.image.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

// Read a texture reference (from pbrMetallicRoughness or a top-level slot)
// and load the underlying image. srgb matches the slot.
static GLuint loadTextureIndex(const tinygltf::Model& model,
                               int texIdx, int srgb) {
    if (texIdx < 0 || texIdx >= (int)model.textures.size()) return 0;
    int imageIdx = model.textures[texIdx].source;
    return loadImageTexture(model, imageIdx, srgb);
}

static Material extractMaterial(const tinygltf::Model& model, int matIdx) {
    Material m;
    if (matIdx < 0 || matIdx >= (int)model.materials.size()) return m;
    const auto& gm = model.materials[matIdx];

    if (gm.pbrMetallicRoughness.baseColorFactor.size() >= 3) {
        m.baseColorFactor = vec3(
            (float)gm.pbrMetallicRoughness.baseColorFactor[0],
            (float)gm.pbrMetallicRoughness.baseColorFactor[1],
            (float)gm.pbrMetallicRoughness.baseColorFactor[2]);
    }
    m.metallicFactor  = (float)gm.pbrMetallicRoughness.metallicFactor;
    m.roughnessFactor = (float)gm.pbrMetallicRoughness.roughnessFactor;
    m.doubleSided     = gm.doubleSided;

    // Basecolor (sRGB)
    m.basecolor = loadTextureIndex(model,
        gm.pbrMetallicRoughness.baseColorTexture.index, 1);

    // Metallic-roughness (linear)
    m.metallicRoughness = loadTextureIndex(model,
        gm.pbrMetallicRoughness.metallicRoughnessTexture.index, 0);

    // Normal (linear)
    if (gm.normalTexture.index >= 0) {
        m.normal = loadTextureIndex(model, gm.normalTexture.index, 0);
        m.normalScale = (float)gm.normalTexture.scale;
    }

    // Emissive (sRGB)
    if (gm.emissiveTexture.index >= 0) {
        m.emissive = loadTextureIndex(model, gm.emissiveTexture.index, 1);
    }
    if (gm.emissiveFactor.size() >= 3) {
        m.emissiveFactor = vec3((float)gm.emissiveFactor[0],
                                (float)gm.emissiveFactor[1],
                                (float)gm.emissiveFactor[2]);
    }

    // Occlusion (linear)
    if (gm.occlusionTexture.index >= 0) {
        m.occlusion = loadTextureIndex(model, gm.occlusionTexture.index, 0);
        m.occlusionStrength = (float)gm.occlusionTexture.strength;
    }

    return m;
}

std::vector<GLBMesh> loadGLB(const std::string& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if (!warn.empty()) std::cerr << "glTF warn: " << warn << "\n";
    if (!ok) { std::cerr << "glTF error: " << err << "\n"; return {}; }

    std::cerr << "[glb] " << path << "\n";
    std::cerr << "  meshes=" << model.meshes.size()
              << " materials=" << model.materials.size()
              << " images=" << model.images.size() << "\n";

    std::vector<GLBMesh> out;

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            auto itPos = prim.attributes.find("POSITION");
            if (itPos == prim.attributes.end()) continue;

            const auto& accPos = model.accessors[itPos->second];
            const auto& bvPos  = model.bufferViews[accPos.bufferView];
            const auto& bufPos = model.buffers[bvPos.buffer];
            const float* pos = reinterpret_cast<const float*>(
                bufPos.data.data() + bvPos.byteOffset + accPos.byteOffset);
            size_t stridePos = bvPos.byteStride ? bvPos.byteStride / sizeof(float) : 3;

            std::vector<vec3> positions;
            positions.reserve(accPos.count);
            for (size_t i = 0; i < accPos.count; ++i)
                positions.push_back(readVec3(pos + i * stridePos));

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

            std::vector<vec4> tangents;
            auto itTan = prim.attributes.find("TANGENT");
            if (itTan != prim.attributes.end()) {
                const auto& acc = model.accessors[itTan->second];
                const auto& bv  = model.bufferViews[acc.bufferView];
                const auto& buf = model.buffers[bv.buffer];
                const float* t = reinterpret_cast<const float*>(
                    buf.data.data() + bv.byteOffset + acc.byteOffset);
                size_t s = bv.byteStride ? bv.byteStride / sizeof(float) : 4;
                tangents.reserve(acc.count);
                for (size_t i = 0; i < acc.count; ++i)
                    tangents.push_back(vec4(t[i*s+0], t[i*s+1], t[i*s+2], t[i*s+3]));
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
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            indices.push_back(base[i]); break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            indices.push_back(reinterpret_cast<const uint16_t*>(base)[i]); break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            indices.push_back(reinterpret_cast<const uint32_t*>(base)[i]); break;
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
                v.tangent = i < tangents.size() ? tangents[i] : vec4(0,0,0,1);
                verts.push_back(v);
            }

            // Fallback: if the model didn't provide tangents, compute them.
            if (tangents.empty()) {
                computeTangents(verts, indices);
            }

            GLBMesh gm;
            gm.mesh     = Mesh(verts, indices);
            gm.material = extractMaterial(model, prim.material);
            out.push_back(std::move(gm));
        }
    }
    std::cerr << "  primitives loaded: " << out.size() << "\n";
    return out;
}
