#pragma once
#include <AYEntity/CameraFrame.h>

namespace ayt::entity
{

class Entity;
class World;
struct OrthoCameraComponent;
struct Transform;

struct SelectedOrthoCamera2D {
    Entity* entity = nullptr;
    Transform* transform = nullptr;
    OrthoCameraComponent* camera = nullptr;

    explicit operator bool() const noexcept {
        return entity != nullptr && transform != nullptr && camera != nullptr;
    }
};

// Select the enabled camera with the greatest serialized priority. Entity id
// then component instance id break ties so every 2D system observes the same
// camera regardless of query storage order.
[[nodiscard]] SelectedOrthoCamera2D selectOrthoCamera2D(World& world, int32_t viewportId = 0) noexcept;
/// Renderer-independent unjittered snapshot using the same query/conversion contract as 3D.
/// Invalid pose/lens fails without changing output; viewport aspect is supplied by the host.
[[nodiscard]] bool evaluateOrthoCamera2D(World& world, float aspect, float interpolationAlpha,
                                        CameraFrame& output, int32_t viewportId = 0);

} // namespace ayt::entity
