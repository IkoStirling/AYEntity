#pragma once
#include <AYMath/MathTypes.h>
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

struct PerspectiveCameraFrame {
    math::Float4x4 view = math::Float4x4::identity();
    math::Float4x4 projection = math::Float4x4::identity();
    math::FVector3 position{};
    uint64_t identity = 0;
    uint64_t cutGeneration = 0;
};

/// Highest active priority wins; Entity id then persistent component id break
/// ties. Borrowed selection is invalid after Entity/component/World destruction.
[[nodiscard]] SelectedPerspectiveCamera3D selectPerspectiveCamera3D(World& world);
/// Renderer-independent evaluation for hosts/tools. Invalid pose returns false
/// and leaves output unchanged; projection parameters are safely sanitized.
[[nodiscard]] bool evaluatePerspectiveCamera3D(World& world, float aspect,
                                               float interpolationAlpha,
                                               PerspectiveCameraFrame& output);
} // namespace ayt::entity
