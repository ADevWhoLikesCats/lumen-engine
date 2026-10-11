#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include "glb_loader.h"

#include <iostream>
#include <unordered_map>

using namespace glm;

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
                verts.push_back(v);
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
