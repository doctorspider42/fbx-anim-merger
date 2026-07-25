#include "gl/Shader.h"

#include <utility>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "util/Log.h"

namespace fam {
namespace {

GLuint CompileStage(GLenum type, const char* source, const char* debugName) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        LogError("Shader '%s' (%s) failed to compile:\n%s", debugName,
                 type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

Shader::~Shader() {
    Destroy();
}

Shader::Shader(Shader&& other) noexcept
    : m_program(other.m_program), m_uniforms(std::move(other.m_uniforms)) {
    other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_program = other.m_program;
        m_uniforms = std::move(other.m_uniforms);
        other.m_program = 0;
    }
    return *this;
}

bool Shader::Build(const char* vertexSource, const char* fragmentSource, const char* debugName) {
    Destroy();

    const GLuint vertex = CompileStage(GL_VERTEX_SHADER, vertexSource, debugName);
    if (!vertex) return false;
    const GLuint fragment = CompileStage(GL_FRAGMENT_SHADER, fragmentSource, debugName);
    if (!fragment) {
        glDeleteShader(vertex);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1), '\0');
        glGetProgramInfoLog(m_program, length, nullptr, log.data());
        LogError("Shader '%s' failed to link:\n%s", debugName, log.data());
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    return true;
}

void Shader::Destroy() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_uniforms.clear();
}

void Shader::Bind() const {
    glUseProgram(m_program);
}

GLint Shader::Location(const char* name) {
    auto it = m_uniforms.find(name);
    if (it != m_uniforms.end()) return it->second;
    const GLint location = glGetUniformLocation(m_program, name);
    m_uniforms.emplace(name, location);
    return location;
}

void Shader::SetInt(const char* name, int value) {
    glUniform1i(Location(name), value);
}
void Shader::SetFloat(const char* name, float value) {
    glUniform1f(Location(name), value);
}
void Shader::SetVec2(const char* name, const glm::vec2& value) {
    glUniform2fv(Location(name), 1, glm::value_ptr(value));
}
void Shader::SetVec3(const char* name, const glm::vec3& value) {
    glUniform3fv(Location(name), 1, glm::value_ptr(value));
}
void Shader::SetVec4(const char* name, const glm::vec4& value) {
    glUniform4fv(Location(name), 1, glm::value_ptr(value));
}
void Shader::SetMat4(const char* name, const glm::mat4& value) {
    glUniformMatrix4fv(Location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::BindUniformBlock(const char* blockName, GLuint bindingPoint) {
    const GLuint index = glGetUniformBlockIndex(m_program, blockName);
    if (index != GL_INVALID_INDEX) glUniformBlockBinding(m_program, index, bindingPoint);
}

}  // namespace fam
