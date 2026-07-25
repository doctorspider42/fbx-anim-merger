#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "util/Log.h"

namespace fam {
namespace {

constexpr GLuint kBoneBinding = 0;

const char* kMeshVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in ivec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;

layout(std140) uniform BoneBlock {
    mat4 uBones[200];
};

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform int uSkinned;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;

void main() {
    vec3 position;
    vec3 normal;

    if (uSkinned == 1) {
        mat4 skin = uBones[aBoneIndices.x] * aBoneWeights.x
                  + uBones[aBoneIndices.y] * aBoneWeights.y
                  + uBones[aBoneIndices.z] * aBoneWeights.z
                  + uBones[aBoneIndices.w] * aBoneWeights.w;
        position = (skin * vec4(aPosition, 1.0)).xyz;
        normal   = mat3(skin) * aNormal;
    } else {
        position = (uModel * vec4(aPosition, 1.0)).xyz;
        normal   = mat3(uModel) * aNormal;
    }

    vWorldPos = position;
    vNormal = normal;
    vUv = aUv;
    gl_Position = uViewProj * vec4(position, 1.0);
}
)";

const char* kMeshFragment = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;

uniform vec4 uBaseColor;
uniform sampler2D uAlbedo;
uniform int uHasTexture;
uniform int uWireframe;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uLightDir;
uniform vec3 uCameraPos;
uniform float uAmbient;
uniform float uExposure;

out vec4 fragColor;

const float PI = 3.14159265359;

float DistributionGGX(vec3 n, vec3 h, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(n, h), 0.0);
    float d = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-5);
}

float GeometrySchlick(float ndotv, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return ndotv / (ndotv * (1.0 - k) + k);
}

void main() {
    vec4 albedo = uBaseColor;
    if (uHasTexture == 1) {
        albedo *= texture(uAlbedo, vUv);
    }
    if (uWireframe == 1) {
        fragColor = vec4(0.55, 0.75, 1.0, 1.0);
        return;
    }

    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCameraPos - vWorldPos);
    if (!gl_FrontFacing) n = -n;

    float roughness = clamp(uRoughness, 0.05, 1.0);
    vec3 f0 = mix(vec3(0.04), albedo.rgb, uMetallic);

    vec3 color = vec3(0.0);

    // Key light plus a dim fill from the opposite side keeps silhouettes readable.
    vec3 lights[2];
    lights[0] = normalize(uLightDir);
    lights[1] = normalize(vec3(-uLightDir.x, 0.4, -uLightDir.z));
    float intensity[2];
    intensity[0] = 1.0;
    intensity[1] = 0.25;

    for (int i = 0; i < 2; ++i) {
        vec3 l = lights[i];
        vec3 h = normalize(v + l);
        float ndotl = max(dot(n, l), 0.0);
        float ndotv = max(dot(n, v), 1e-4);
        if (ndotl <= 0.0) continue;

        float d = DistributionGGX(n, h, roughness);
        float g = GeometrySchlick(ndotv, roughness) * GeometrySchlick(ndotl, roughness);
        vec3 f = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);

        vec3 specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
        vec3 diffuse = (1.0 - f) * (1.0 - uMetallic) * albedo.rgb / PI;
        color += (diffuse + specular) * ndotl * intensity[i];
    }

    // Hemispherical ambient: cool from below, warm from above.
    vec3 skyColor = vec3(0.42, 0.48, 0.58);
    vec3 groundColor = vec3(0.16, 0.15, 0.14);
    vec3 ambient = mix(groundColor, skyColor, n.y * 0.5 + 0.5) * albedo.rgb * uAmbient;
    color += ambient;

    color *= uExposure;
    color = color / (color + vec3(1.0));           // Reinhard
    color = pow(color, vec3(1.0 / 2.2));           // gamma
    fragColor = vec4(color, albedo.a);
}
)";

const char* kGridVertex = R"(#version 330 core
out vec2 vNdc;
void main() {
    // Oversized triangle covering the whole viewport, no vertex buffer needed.
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vNdc = positions[gl_VertexID];
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)";

const char* kGridFragment = R"(#version 330 core
in vec2 vNdc;

uniform mat4 uInvViewProj;
uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform float uFadeDistance;

out vec4 fragColor;

float GridFactor(vec2 coord, float spacing) {
    vec2 uv = coord / spacing;
    vec2 derivative = fwidth(uv);
    vec2 grid = abs(fract(uv - 0.5) - 0.5) / max(derivative, vec2(1e-6));
    return 1.0 - min(min(grid.x, grid.y), 1.0);
}

void main() {
    vec4 nearPoint = uInvViewProj * vec4(vNdc, -1.0, 1.0);
    vec4 farPoint  = uInvViewProj * vec4(vNdc,  1.0, 1.0);
    vec3 origin = nearPoint.xyz / nearPoint.w;
    vec3 direction = farPoint.xyz / farPoint.w - origin;

    float t = -origin.y / direction.y;
    if (t <= 0.0 || abs(direction.y) < 1e-6) discard;

    vec3 world = origin + direction * t;

    float minor = GridFactor(world.xz, 1.0);
    float major = GridFactor(world.xz, 10.0);
    float alpha = max(minor * 0.35, major * 0.6);
    if (alpha < 0.002) discard;

    vec3 color = vec3(0.55);
    vec2 axisWidth = fwidth(world.xz) * 1.2;
    if (abs(world.z) < axisWidth.y) color = vec3(0.85, 0.28, 0.32);   // X axis
    if (abs(world.x) < axisWidth.x) color = vec3(0.32, 0.55, 0.92);   // Z axis

    float distance = length(world - uCameraPos);
    alpha *= 1.0 - smoothstep(uFadeDistance * 0.35, uFadeDistance, distance);
    if (alpha < 0.002) discard;

    vec4 clip = uViewProj * vec4(world, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), alpha);
}
)";

const char* kLineVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProj;
void main() {
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
)";

const char* kLineFragment = R"(#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

}  // namespace

bool Renderer::Initialize() {
    if (!m_meshShader.Build(kMeshVertex, kMeshFragment, "mesh")) return false;
    if (!m_gridShader.Build(kGridVertex, kGridFragment, "grid")) return false;
    if (!m_lineShader.Build(kLineVertex, kLineFragment, "line")) return false;

    m_meshShader.Bind();
    m_meshShader.BindUniformBlock("BoneBlock", kBoneBinding);
    m_meshShader.SetInt("uAlbedo", 0);

    glGenBuffers(1, &m_boneUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, m_boneUbo);
    glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(sizeof(glm::mat4) * kMaxBones), nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, kBoneBinding, m_boneUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glGenVertexArrays(1, &m_emptyVao);

    glGenVertexArrays(1, &m_lineVao);
    glBindVertexArray(m_lineVao);
    glGenBuffers(1, &m_lineVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);

    return true;
}

void Renderer::Destroy() {
    DestroyTargets();
    if (m_boneUbo) glDeleteBuffers(1, &m_boneUbo);
    if (m_lineVbo) glDeleteBuffers(1, &m_lineVbo);
    if (m_lineVao) glDeleteVertexArrays(1, &m_lineVao);
    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
    m_boneUbo = m_lineVbo = m_lineVao = m_emptyVao = 0;
    m_meshShader.Destroy();
    m_gridShader.Destroy();
    m_lineShader.Destroy();
}

void Renderer::DestroyTargets() {
    if (m_msaaFbo) glDeleteFramebuffers(1, &m_msaaFbo);
    if (m_resolveFbo) glDeleteFramebuffers(1, &m_resolveFbo);
    if (m_msaaColor) glDeleteRenderbuffers(1, &m_msaaColor);
    if (m_msaaDepth) glDeleteRenderbuffers(1, &m_msaaDepth);
    if (m_resolveColor) glDeleteTextures(1, &m_resolveColor);
    m_msaaFbo = m_resolveFbo = m_msaaColor = m_msaaDepth = m_resolveColor = 0;
}

void Renderer::Resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == m_width && height == m_height && m_msaaFbo != 0) return;

    DestroyTargets();
    m_width = width;
    m_height = height;

    glGenRenderbuffers(1, &m_msaaColor);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_RGBA8, width, height);

    glGenRenderbuffers(1, &m_msaaDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &m_msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColor);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LogWarn("Multisampled framebuffer incomplete, falling back to 1 sample.");
        m_samples = 1;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        DestroyTargets();
        m_width = m_height = 0;
        Resize(width, height);
        return;
    }

    glGenTextures(1, &m_resolveColor);
    glBindTexture(GL_TEXTURE_2D, m_resolveColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveColor, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LogError("Resolve framebuffer incomplete.");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::DrawGrid(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    m_gridShader.Bind();
    m_gridShader.SetMat4("uInvViewProj", glm::inverse(viewProj));
    m_gridShader.SetMat4("uViewProj", viewProj);
    m_gridShader.SetVec3("uCameraPos", cameraPos);
    m_gridShader.SetFloat("uFadeDistance", std::max(40.0f, glm::length(cameraPos) * 6.0f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    ++m_drawCalls;
}

void Renderer::DrawSkeleton(const Model& model, const PoseEvaluator& pose, const glm::mat4& viewProj,
                            bool xray) {
    const std::vector<glm::mat4>& globals = pose.Globals();
    if (globals.size() != model.nodes.size()) return;

    m_lineVertices.clear();
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const Node& node = model.nodes[i];
        if (node.parent < 0) continue;
        // Only draw links where at least one end is a skinning bone; otherwise a
        // rig's helper/mesh nodes bury the actual skeleton.
        const bool self = model.skeleton.boneByName.count(node.name) != 0;
        const bool parent =
            model.skeleton.boneByName.count(model.nodes[static_cast<size_t>(node.parent)].name) != 0;
        if (!self && !parent) continue;

        m_lineVertices.push_back(glm::vec3(globals[static_cast<size_t>(node.parent)][3]));
        m_lineVertices.push_back(glm::vec3(globals[i][3]));
    }
    if (m_lineVertices.empty()) return;

    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    const size_t bytes = m_lineVertices.size() * sizeof(glm::vec3);
    if (m_lineVertices.size() > m_lineCapacity) {
        m_lineCapacity = m_lineVertices.size() * 2;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_lineCapacity * sizeof(glm::vec3)),
                     nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), m_lineVertices.data());

    m_lineShader.Bind();
    m_lineShader.SetMat4("uViewProj", viewProj);

    if (xray) glDisable(GL_DEPTH_TEST);
    glLineWidth(1.5f);
    m_lineShader.SetVec4("uColor", glm::vec4(1.0f, 0.72f, 0.25f, 1.0f));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_lineVertices.size()));
    if (xray) glEnable(GL_DEPTH_TEST);

    glBindVertexArray(0);
    ++m_drawCalls;
}

void Renderer::Render(const Model& model, const GpuModel& gpu, const PoseEvaluator& pose,
                      const OrbitCamera& camera, const RenderSettings& settings) {
    if (m_msaaFbo == 0) return;

    m_drawCalls = 0;
    m_triangles = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glViewport(0, 0, m_width, m_height);
    if (m_samples > 1) glEnable(GL_MULTISAMPLE);

    const glm::vec3 bg = glm::pow(settings.background, glm::vec3(1.0f / 2.2f));
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    if (settings.backfaceCulling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }

    const float aspect = static_cast<float>(m_width) / static_cast<float>(std::max(m_height, 1));
    const glm::mat4 viewProj = camera.Projection(aspect) * camera.View();
    const glm::vec3 cameraPos = camera.Position();

    const glm::vec3 lightDir = glm::normalize(glm::vec3(std::sin(settings.lightYaw) * std::cos(settings.lightPitch),
                                                        std::sin(settings.lightPitch),
                                                        std::cos(settings.lightYaw) * std::cos(settings.lightPitch)));

    if (settings.showMesh && gpu.Ready()) {
        const std::vector<glm::mat4>& boneMatrices = pose.BoneMatrices();
        if (!boneMatrices.empty()) {
            const size_t count = std::min<size_t>(boneMatrices.size(), kMaxBones);
            glBindBuffer(GL_UNIFORM_BUFFER, m_boneUbo);
            glBufferSubData(GL_UNIFORM_BUFFER, 0,
                            static_cast<GLsizeiptr>(count * sizeof(glm::mat4)), boneMatrices.data());
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }

        m_meshShader.Bind();
        m_meshShader.SetMat4("uViewProj", viewProj);
        m_meshShader.SetVec3("uCameraPos", cameraPos);
        m_meshShader.SetVec3("uLightDir", lightDir);
        m_meshShader.SetFloat("uAmbient", settings.ambient);
        m_meshShader.SetFloat("uExposure", settings.exposure);
        m_meshShader.SetInt("uWireframe", settings.wireframe ? 1 : 0);

        if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        const std::vector<glm::mat4>& globals = pose.Globals();
        const std::vector<GpuMaterial>& materials = gpu.Materials();

        for (const GpuMesh& mesh : gpu.Meshes()) {
            glm::mat4 modelMatrix(1.0f);
            if (!mesh.skinned && mesh.nodeIndex >= 0 &&
                static_cast<size_t>(mesh.nodeIndex) < globals.size()) {
                modelMatrix = globals[static_cast<size_t>(mesh.nodeIndex)];
            }
            m_meshShader.SetMat4("uModel", modelMatrix);
            m_meshShader.SetInt("uSkinned", mesh.skinned && !boneMatrices.empty() ? 1 : 0);

            glBindVertexArray(mesh.vao);
            for (const GpuSubMesh& sub : mesh.subMeshes) {
                const GpuMaterial& material =
                    materials[static_cast<size_t>(
                        sub.materialIndex >= 0 && sub.materialIndex < static_cast<int>(materials.size())
                            ? sub.materialIndex
                            : 0)];

                m_meshShader.SetVec4("uBaseColor", material.baseColor);
                m_meshShader.SetFloat("uMetallic", material.metallic);
                m_meshShader.SetFloat("uRoughness", material.roughness);
                m_meshShader.SetInt("uHasTexture", material.texture ? 1 : 0);
                if (material.texture) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, material.texture);
                }

                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                               reinterpret_cast<void*>(static_cast<uintptr_t>(sub.indexOffset) *
                                                       sizeof(uint32_t)));
                ++m_drawCalls;
                m_triangles += sub.indexCount / 3;
            }
            glBindVertexArray(0);
        }

        if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    if (settings.showGrid) DrawGrid(viewProj, cameraPos);
    if (settings.showSkeleton && !model.skeleton.Empty()) {
        DrawSkeleton(model, pose, viewProj, settings.xray);
    }

    // Resolve MSAA into the texture ImGui samples.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (m_samples > 1) glDisable(GL_MULTISAMPLE);
}

}  // namespace fam
