#pragma once
#include <glad/glad.h>
#include <string>

struct Texture {
    GLuint id = 0;
    int    width = 0, height = 0;

    Texture() = default;

    // Loads via stb_image. Flips vertically by default (matches most
    // DCC exports where UV (0,0) is bottom-left). Pass flip=false for
    // textures authored top-left (raw GLB embedded images often are).
    explicit Texture(const std::string& path, bool flip = true);

    // Non-copyable, movable — owns a GL handle.
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& o) noexcept { *this = std::move(o); }
    Texture& operator=(Texture&& o) noexcept {
        std::swap(id, o.id);
        std::swap(width, o.width);
        std::swap(height, o.height);
        return *this;
    }

    void bind(int unit = 0) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, id);
    }

    ~Texture();
};
