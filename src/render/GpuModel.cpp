#include "render/GpuModel.h"

#include <cstddef>

#include "util/Log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#include <stb_image.h>

namespace fam {

GpuModel::~GpuModel() {
    Destroy();
}

void GpuModel::Destroy() {
    for (GpuMesh& mesh : m_meshes) {
        if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
        if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    }
    m_meshes.clear();

    for (auto& [path, texture] : m_textures) {
        if (texture) glDeleteTextures(1, &texture);
    }
    m_textures.clear();
    m_materials.clear();
}

GLuint GpuModel::LoadTexture(const TextureSource& source) {
    if (source.Empty()) return 0;

    const std::string& key = source.Key();
    auto it = m_textures.find(key);
    if (it != m_textures.end()) return it->second;

    int width = 0;
    int height = 0;
    int channels = 0;
    // FBX UVs put (0,0) at the bottom-left, OpenGL samples an unflipped upload from
    // the top-left, so the image is flipped once here rather than in the shader.
    stbi_set_flip_vertically_on_load(1);

    unsigned char* pixels = nullptr;
    if (source.Embedded()) {
        pixels = stbi_load_from_memory(source.content->data(),
                                       static_cast<int>(source.content->size()), &width, &height,
                                       &channels, 4);
    } else {
        pixels = stbi_load(source.path.c_str(), &width, &height, &channels, 4);
    }

    if (!pixels) {
        LogWarn("Could not decode texture '%s' (%s).", key.c_str(), stbi_failure_reason());
        m_textures.emplace(key, 0);
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    m_textures.emplace(key, texture);
    LogInfo("Texture '%s' %dx%d loaded (%s).", key.c_str(), width, height,
            source.Embedded() ? "embedded" : "from disk");
    return texture;
}

void GpuModel::Upload(const Model& model) {
    Destroy();

    m_materials.reserve(model.materials.size());
    for (const Material& material : model.materials) {
        GpuMaterial gpu;
        gpu.baseColor = glm::vec4(material.baseColor, material.opacity);
        gpu.metallic = material.metallic;
        gpu.roughness = material.roughness;
        gpu.texture = LoadTexture(material.baseColorTexture);
        m_materials.push_back(gpu);
    }
    if (m_materials.empty()) m_materials.push_back(GpuMaterial{});

    m_meshes.reserve(model.meshes.size());
    for (const Mesh& mesh : model.meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty()) continue;

        GpuMesh gpu;
        gpu.skinned = mesh.skinned;
        gpu.nodeIndex = mesh.nodeIndex;

        glGenVertexArrays(1, &gpu.vao);
        glBindVertexArray(gpu.vao);

        glGenBuffers(1, &gpu.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                     mesh.vertices.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &gpu.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                     mesh.indices.data(), GL_STATIC_DRAW);

        const GLsizei stride = sizeof(Vertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, uv)));
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 4, GL_INT, stride,
                               reinterpret_cast<void*>(offsetof(Vertex, boneIndices)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, boneWeights)));

        glBindVertexArray(0);

        for (const SubMesh& sub : mesh.subMeshes) {
            gpu.subMeshes.push_back({sub.indexOffset, sub.indexCount, sub.materialIndex});
        }
        if (gpu.subMeshes.empty()) {
            gpu.subMeshes.push_back({0, static_cast<uint32_t>(mesh.indices.size()), 0});
        }

        m_meshes.push_back(gpu);
    }
}

}  // namespace fam
