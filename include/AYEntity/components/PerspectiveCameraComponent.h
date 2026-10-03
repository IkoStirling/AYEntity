#pragma once

#include <AYCore.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYMath/MathUtils.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ayt::entity {

#define AY_CURRENT_CLASS PerspectiveCameraComponent
/// Scene-owned 3D gameplay camera. LH, Y-up, local +Z forward; pose comes
/// from Transform (scale ignored). Parameters/selection persist in Scene.
struct PerspectiveCameraComponent : IComponent {
    const char* getName() const override { return "PerspectiveCameraComponent"; }
    AY_PROPERTY(float, fovYDegrees, kAttrSerialize)
    AY_PROPERTY(float, nearZ, kAttrSerialize)
    AY_PROPERTY(float, farZ, kAttrSerialize)
    AY_PROPERTY(bool, active, kAttrSerialize)
    AY_PROPERTY(int32_t, priority, kAttrSerialize)

    PerspectiveCameraComponent()
        : fovYDegrees(50.0f), nearZ(0.1f), farZ(100.0f), active(true), priority(0) {}

    /// Call after a teleport/discontinuous lens change, even below automatic
    /// camera-cut thresholds. Runtime-only; new/load/Play World changes cut too.
    void requestCameraCut() noexcept { ++cutGeneration; }
    uint64_t cutGeneration = 0;

    [[nodiscard]] math::Float4x4 projectionMatrix(float viewportAspect) const noexcept {
        const float fov = std::isfinite(fovYDegrees)
            ? std::clamp(fovYDegrees, 1.0f, 179.0f) : 50.0f;
        const float aspect = std::isfinite(viewportAspect) && viewportAspect > 1.0e-6f
            ? viewportAspect : 1.0f;
        const float nearPlane = std::isfinite(nearZ) && nearZ >= 1.0e-4f && nearZ <= 1.0e6f
            ? nearZ : 0.1f;
        const float farPlane = std::isfinite(farZ) && farZ > nearPlane && farZ <= 1.0e8f
            ? farZ : nearPlane + 100.0f;
        return math::lh::perspective(math::radians(fov), aspect, nearPlane, farPlane);
    }

    [[nodiscard]] math::Float4x4 viewMatrix(const math::FVector3& position,
                                           const math::FQuaternion& orientation) const noexcept {
        const auto rotation = orientation.normalize().toMatrix();
        const math::FVector3 forward{rotation(0, 2), rotation(1, 2), rotation(2, 2)};
        const math::FVector3 up{rotation(0, 1), rotation(1, 1), rotation(2, 1)};
        return math::lh::lookAt(position, position + forward, up);
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
