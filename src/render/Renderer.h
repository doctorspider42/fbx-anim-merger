#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/Model.h"
#include "core/Pose.h"
#include "gl/GL.h"
#include "gl/Shader.h"
#include "render/Camera.h"
#include "render/GpuModel.h"

namespace fam {

struct RenderSettings {
    bool showGrid = true;
    bool showSkeleton = false;
    bool showMesh = true;
    bool wireframe = false;
    bool backfaceCulling = false;
    bool xray = true;  // skeleton drawn through geometry

    glm::vec3 background{0.10f, 0.11f, 0.13f};
    float lightYaw = 0.9f;
    float lightPitch = 0.9f;
    float ambient = 0.35f;
    float exposure = 1.0f;
};

// Renders into an offscreen (optionally multisampled) target so the viewport can
// be composited as an ImGui image.
class Renderer {
public:
    bool Initialize();
    void Destroy();

    void Resize(int width, int height);

    void Render(const Model& model, const GpuModel& gpu, const PoseEvaluator& pose,
                const OrbitCamera& camera, const RenderSettings& settings);

    GLuint ColorTexture() const { return m_resolveColor; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }

    size_t LastDrawCalls() const { return m_drawCalls; }
    size_t LastTriangles() const { return m_triangles; }

private:
    void DestroyTargets();
    void DrawGrid(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void DrawSkeleton(const Model& model, const PoseEvaluator& pose, const glm::mat4& viewProj,
                      bool xray);

    Shader m_meshShader;
    Shader m_gridShader;
    Shader m_lineShader;

    GLuint m_boneUbo = 0;
    GLuint m_emptyVao = 0;

    GLuint m_lineVao = 0;
    GLuint m_lineVbo = 0;
    size_t m_lineCapacity = 0;
    std::vector<glm::vec3> m_lineVertices;

    GLuint m_msaaFbo = 0;
    GLuint m_msaaColor = 0;
    GLuint m_msaaDepth = 0;
    GLuint m_resolveFbo = 0;
    GLuint m_resolveColor = 0;

    int m_width = 0;
    int m_height = 0;
    int m_samples = 4;

    size_t m_drawCalls = 0;
    size_t m_triangles = 0;
};

}  // namespace fam
