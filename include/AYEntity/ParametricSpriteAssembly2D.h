#pragma once

#include <AY2D/ParametricSpriteRig2D.h>
#include <AYEntity/EntityHandle.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ayt::entity {
class Entity;
class World;

/// Owns one stable Sprite Entity per layer of a rigid-only puppet in a normal
/// World. Any authored mesh layer is rejected.
/// The root owns all layer Entities; spatial parentage follows the Rig. The
/// host calls destroy before World shutdown and owns the live Rig and textures.
class ParametricSpriteAssembly2D {
public:
    ParametricSpriteAssembly2D() = default;
    ParametricSpriteAssembly2D(const ParametricSpriteAssembly2D&) = delete;
    ParametricSpriteAssembly2D& operator=(const ParametricSpriteAssembly2D&) = delete;

    /// Create a new root and all layers, then present the current pose.
    /// This object must be empty. On failure every newly created Entity is
    /// removed and the object remains empty. A Spatial parent is optional.
    [[nodiscard]] bool create(World& world,
        const ay2d::ParametricSpriteRig2D& rig, Entity* spatialParent = nullptr,
        std::string* error = nullptr);

    /// Evaluate and update all layers without changing Entity identities.
    /// Reject a changed layer/parent topology or missing Entity before writes.
    [[nodiscard]] bool present(const ay2d::ParametricSpriteRig2D& rig,
                               std::uint32_t sortBase = 0);

    /// Destroy the owned root and its layers. A rejected World destruction
    /// retains the handles so the caller can retry after resolving the cause.
    [[nodiscard]] bool destroy(std::string* error = nullptr);

    [[nodiscard]] Entity* root() const noexcept;
    [[nodiscard]] Entity* layer(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t layerCount() const noexcept
    { return layers_.size(); }

private:
    World* world_ = nullptr;
    EntityHandle root_{};
    std::vector<EntityHandle> layers_;
    std::vector<std::string> names_;
    std::vector<std::int32_t> parents_;
};

} // namespace ayt::entity
