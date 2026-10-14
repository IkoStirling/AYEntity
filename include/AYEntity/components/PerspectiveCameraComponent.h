#pragma once

#include <AYCore.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/CameraFrame.h>
#include <AYMath/MathUtils.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ayt::entity {

#define AY_CURRENT_CLASS PerspectiveCameraComponent
/// Scene-owned perspective/orthographic 3D camera (name retained for Scene compatibility).
/// LH, Y-up, local +Z forward; pose comes
/// from Transform (scale ignored). Parameters/selection persist in Scene.
struct PerspectiveCameraComponent : IComponent {
    const char* getName() const override { return "PerspectiveCameraComponent"; }
    AY_PROPERTY(float, fovYDegrees, kAttrSerialize)
    AY_PROPERTY(float, nearZ, kAttrSerialize)
    AY_PROPERTY(float, farZ, kAttrSerialize)
    AY_PROPERTY(bool, active, kAttrSerialize)
    AY_PROPERTY(int32_t, priority, kAttrSerialize)
    /// 0=Perspective, 1=Orthographic; existing scenes default to perspective.
    AY_PROPERTY(int32_t, projectionMode, kAttrSerialize)
    AY_PROPERTY(float, orthographicHeight, kAttrSerialize)
    AY_PROPERTY(math::FVector3, localOffset, kAttrSerialize)
    AY_PROPERTY(math::FQuaternion, localRotation, kAttrSerialize)
    AY_PROPERTY(uint32_t, layerMask, kAttrSerialize)
    /// Select cameras independently for numbered runtime viewports (0 is the legacy view).
    AY_PROPERTY(int32_t, viewportId, kAttrSerialize)
    AY_PROPERTY(float, blendDuration, kAttrSerialize)

    PerspectiveCameraComponent()
        : fovYDegrees(50.0f), nearZ(0.1f), farZ(100.0f), active(true), priority(0),
          projectionMode(0), orthographicHeight(10), localOffset(0,0,0),
          localRotation(0,0,0,1), layerMask(0xFFFFFFFFu), viewportId(0), blendDuration(0) {}

    /// Call after a teleport/discontinuous lens change, even below automatic
    /// camera-cut thresholds. Runtime-only; new/load/Play World changes cut too.
    void requestCameraCut() noexcept { ++cutGeneration; }
    uint64_t cutGeneration = 0;

    /// Atomically discard Transform interpolation and mark a discontinuity for rendering.
    void teleport(Transform& transform, const math::FVector3& position,
                  const math::FQuaternion& orientation) noexcept {
        transform.setPresentationPose(position, orientation); requestCameraCut();
    }

    [[nodiscard]] math::Float4x4 projectionMatrix(float viewportAspect) const noexcept {
        const float fov = std::isfinite(fovYDegrees)
            ? std::clamp(fovYDegrees, 1.0f, 179.0f) : 50.0f;
        const float aspect = std::isfinite(viewportAspect) && viewportAspect > 1.0e-6f
            ? viewportAspect : 1.0f;
        const float nearPlane = std::isfinite(nearZ) && nearZ >= 1.0e-4f && nearZ <= 1.0e6f
            ? nearZ : 0.1f;
        const float farPlane = std::isfinite(farZ) && farZ > nearPlane && farZ <= 1.0e8f
            ? farZ : nearPlane + 100.0f;
        if (projectionMode == 1) {
            const float height = std::isfinite(orthographicHeight) && orthographicHeight > 1e-4f
                ? orthographicHeight : 10.f;
            return math::lh::ortho(-height * aspect * .5f, height * aspect * .5f,
                -height * .5f, height * .5f, nearPlane, farPlane);
        }
        return math::lh::perspective(math::radians(fov), aspect, nearPlane, farPlane);
    }

    [[nodiscard]] math::Float4x4 viewMatrix(const math::FVector3& position,
                                           const math::FQuaternion& orientation) const noexcept {
        const auto rotation = orientation.normalize().toMatrix();
        // Compose the inverse pose directly; eye+unit-forward can lose its direction
        // at large coordinates even when the quaternion remains perfectly valid.
        return rotation.transpose() * math::translate(-position.x,-position.y,-position.z);
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
