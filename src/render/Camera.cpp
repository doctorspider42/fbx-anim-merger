#include "render/Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace fam {
namespace {
constexpr float kPitchLimit = 1.5533f;  // ~89 degrees
}

void OrbitCamera::Orbit(float deltaYaw, float deltaPitch) {
    m_yaw += deltaYaw;
    m_pitch = std::clamp(m_pitch + deltaPitch, -kPitchLimit, kPitchLimit);
}

void OrbitCamera::Pan(float deltaX, float deltaY) {
    const glm::vec3 forward = glm::normalize(m_target - Position());
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    // Scale with distance so panning feels identical at any zoom level.
    const float speed = m_distance * 0.0015f;
    m_target += (-right * deltaX + up * deltaY) * speed;
}

void OrbitCamera::Dolly(float amount) {
    m_distance = std::clamp(m_distance * std::pow(0.9f, amount), 0.02f, 2000.0f);
}

void OrbitCamera::FrameBounds(const glm::vec3& min, const glm::vec3& max) {
    const glm::vec3 center = (min + max) * 0.5f;
    const float radius = std::max(glm::length(max - min) * 0.5f, 0.05f);

    m_target = center;
    m_distance = radius / std::tan(glm::radians(fovDegrees) * 0.5f) * 1.6f;
    m_near = std::max(radius * 0.001f, 0.001f);
    m_far = std::max(m_distance + radius * 8.0f, 50.0f);
    m_yaw = 0.6f;
    m_pitch = 0.18f;
}

void OrbitCamera::Reset() {
    m_target = glm::vec3(0.0f, 1.0f, 0.0f);
    m_distance = 4.0f;
    m_yaw = 0.6f;
    m_pitch = 0.25f;
}

glm::vec3 OrbitCamera::Position() const {
    const float cosPitch = std::cos(m_pitch);
    const glm::vec3 offset{std::sin(m_yaw) * cosPitch, std::sin(m_pitch), std::cos(m_yaw) * cosPitch};
    return m_target + offset * m_distance;
}

glm::mat4 OrbitCamera::View() const {
    return glm::lookAt(Position(), m_target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::Projection(float aspect) const {
    return glm::perspective(glm::radians(fovDegrees), std::max(aspect, 0.0001f), m_near, m_far);
}

}  // namespace fam
