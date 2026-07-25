#pragma once

#include <glm/glm.hpp>

namespace fam {

// Turntable camera: LMB orbits, MMB/Shift+LMB pans, wheel dollies.
class OrbitCamera {
public:
    void Orbit(float deltaYaw, float deltaPitch);
    void Pan(float deltaX, float deltaY);
    void Dolly(float amount);

    void FrameBounds(const glm::vec3& min, const glm::vec3& max);
    void Reset();

    glm::mat4 View() const;
    glm::mat4 Projection(float aspect) const;
    glm::vec3 Position() const;

    const glm::vec3& Target() const { return m_target; }
    float Distance() const { return m_distance; }
    float NearPlane() const { return m_near; }
    float FarPlane() const { return m_far; }

    float fovDegrees = 45.0f;

private:
    glm::vec3 m_target{0.0f, 1.0f, 0.0f};
    float m_distance = 4.0f;
    float m_yaw = 0.6f;    // radians
    float m_pitch = 0.25f; // radians
    float m_near = 0.02f;
    float m_far = 500.0f;
};

}  // namespace fam
