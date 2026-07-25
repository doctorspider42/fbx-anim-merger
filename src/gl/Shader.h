#pragma once

#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "gl/GL.h"

namespace fam {

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    bool Build(const char* vertexSource, const char* fragmentSource, const char* debugName);
    void Destroy();

    void Bind() const;
    GLuint Id() const { return m_program; }
    bool Valid() const { return m_program != 0; }

    void SetInt(const char* name, int value);
    void SetFloat(const char* name, float value);
    void SetVec2(const char* name, const glm::vec2& value);
    void SetVec3(const char* name, const glm::vec3& value);
    void SetVec4(const char* name, const glm::vec4& value);
    void SetMat4(const char* name, const glm::mat4& value);

    void BindUniformBlock(const char* blockName, GLuint bindingPoint);

private:
    GLint Location(const char* name);

    GLuint m_program = 0;
    std::unordered_map<std::string, GLint> m_uniforms;
};

}  // namespace fam
