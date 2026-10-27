#pragma once

#include <AYEntity/EntityHandle.h>
#include <AYPhysics/IPhysicsQuery.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ayt::entity {
class Entity;
class World;

/// A fresh game-owned view. ignoredBody lets a character ray pass its own
/// collider; the backend must support that query or focus fails explicitly.
struct WorldInteractionView3D {
    math::Ray ray{};
    double reach = 0;
    physics::PhysLayerMask layerMask = 0xFFFFFFFFu;
    physics::BodyHandle ignoredBody = physics::InvalidBodyHandle;
};

/// One currently visible Spatial Entity and its game-defined action IDs.
/// This is an observation, not authorization to activate it later.
struct WorldInteractionFocus3D {
    EntityHandle entity{};
    std::string objectId;
    physics::BodyHandle body = physics::InvalidBodyHandle;
    physics::ColliderHandle collider = physics::InvalidColliderHandle;
    math::FVector3 point{}, normal{};
    float distance = 0;
    std::vector<std::uint32_t> actions;
};

enum class WorldInteractionStatus3D : std::uint8_t {
    Ok, Invalid, Unavailable, Stale, Denied, Failed
};

struct WorldInteractionFocusResult3D {
    WorldInteractionStatus3D status = WorldInteractionStatus3D::Ok;
    std::optional<WorldInteractionFocus3D> focus;
};

struct WorldInteractionReceipt3D {
    std::uint64_t requestId = 0;
    std::string objectId;
    std::uint32_t actionId = 0;
};

struct WorldInteractionActivationResult3D {
    WorldInteractionStatus3D status = WorldInteractionStatus3D::Ok;
    std::optional<WorldInteractionReceipt3D> receipt;
};

/// A thin 3D focus and activation protocol. The Host supplies current view,
/// body-to-Entity lookup, authored action offers, permission and failure-atomic
/// commit. No game verbs, rules, UI, persistence or event bus are built in.
/// Borrowed World/query and callbacks must remain valid on the Sim owner thread.
class WorldInteraction3D {
public:
    using ViewProvider = std::function<std::optional<WorldInteractionView3D>(
        std::string_view actorId)>;
    using BodyResolver = std::function<Entity*(physics::BodyHandle)>;
    using Offers = std::function<std::vector<std::uint32_t>(
        const Entity&, std::string_view actorId)>;
    using Permission = std::function<bool(std::string_view actorId,
        const WorldInteractionFocus3D&, std::uint32_t actionId)>;
    using Commit = std::function<bool(std::uint64_t requestId,
        std::string_view actorId, const WorldInteractionFocus3D&,
        std::uint32_t actionId)>;

    WorldInteraction3D(World& world, physics::IPhysicsQuery& query,
        ViewProvider view, BodyResolver resolve, Offers offers,
        Permission permission, Commit commit);

    /// Raycast the current view once. A nearest non-offering solid blocks
    /// interaction behind it. At most 16 unique nonzero actions are accepted.
    [[nodiscard]] WorldInteractionFocusResult3D focus(
        std::string_view actorId) const;

    /// Requery current focus and offers, then authorize and commit. A selected
    /// focus is never authority. A nonzero requestId is forwarded to the game
    /// callback, which owns durable deduplication and atomic side effects.
    [[nodiscard]] WorldInteractionActivationResult3D activate(
        std::string_view actorId, const WorldInteractionFocus3D& selected,
        std::uint32_t actionId, std::uint64_t requestId) const;

private:
    World& world_;
    physics::IPhysicsQuery& query_;
    ViewProvider view_;
    BodyResolver resolve_;
    Offers offers_;
    Permission permission_;
    Commit commit_;
};

} // namespace ayt::entity
