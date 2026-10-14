#pragma once
#include <AYEntity/CameraFrame.h>
#include <cstdint>

namespace ayt::entity {
class Entity;
class World;
struct Transform;
struct PerspectiveCameraComponent;

struct SelectedPerspectiveCamera3D {
    Entity* entity = nullptr;
    Transform* transform = nullptr;
    PerspectiveCameraComponent* camera = nullptr;
    explicit operator bool() const noexcept { return entity && transform && camera; }
};

using PerspectiveCameraFrame = CameraFrame;

/// Highest active priority wins; Entity id then persistent component id break
/// ties. Borrowed selection is invalid after Entity/component/World destruction.
[[nodiscard]] SelectedPerspectiveCamera3D selectPerspectiveCamera3D(World& world, int32_t viewportId = 0);
/// Renderer-independent evaluation for hosts/tools. Invalid pose returns false
/// and leaves output unchanged; projection parameters are safely sanitized.
[[nodiscard]] bool evaluatePerspectiveCamera3D(World& world, float aspect,
                                               float interpolationAlpha,
                                               PerspectiveCameraFrame& output, int32_t viewportId = 0);
} // namespace ayt::entity
