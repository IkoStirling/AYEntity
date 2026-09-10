#pragma once

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
// breaks ties so every 2D system observes the same camera regardless of query
// storage order.
[[nodiscard]] SelectedOrthoCamera2D selectOrthoCamera2D(World& world) noexcept;

} // namespace ayt::entity
