#include "texture.h"
#include <iostream>

// STB_IMAGE_IMPLEMENTATION is defined in glb_loader.cpp, so we just
// include the header here for the declarations.
#include <stb_image.h>
#include <GLFW/glfw3.h>

Texture::Texture(const std::string& path, bool flip) {
    stbi_set_flip_vertically_on_load(flip ? 1 : 0);

    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 3);
    if (!data) {
        std::cerr << "Texture load failed: " << path
                  << " (" << stbi_failure_reason() << ")\n";
        return;
    }

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
}

Texture::~Texture() {
    // Only delete if the GL context is still alive. Callers that
    // destroy this object after glfwTerminate must accept the leak
    // (there is no context to call glDeleteTextures on).
    if (id && glfwGetCurrentContext()) glDeleteTextures(1, &id);
}
