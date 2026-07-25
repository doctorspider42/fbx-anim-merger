#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Model.h"
#include "gl/GL.h"

namespace fam {

struct GpuSubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    int materialIndex = -1;
};

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    bool skinned = false;
    int nodeIndex = -1;
    std::vector<GpuSubMesh> subMeshes;
};

struct GpuMaterial {
    glm::vec4 baseColor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.6f;
    GLuint texture = 0;  // 0 = untextured
};

// GPU mirror of a Model. Rebuilt whenever geometry changes; animation playback
// only touches the bone uniform buffer.
class GpuModel {
public:
    ~GpuModel();

    void Upload(const Model& model);
    void Destroy();

    bool Ready() const { return !m_meshes.empty(); }
    const std::vector<GpuMesh>& Meshes() const { return m_meshes; }
    const std::vector<GpuMaterial>& Materials() const { return m_materials; }

    size_t TextureCount() const { return m_textures.size(); }

private:
    GLuint LoadTexture(const TextureSource& source);

    std::vector<GpuMesh> m_meshes;
    std::vector<GpuMaterial> m_materials;
    std::unordered_map<std::string, GLuint> m_textures;
};

}  // namespace fam
