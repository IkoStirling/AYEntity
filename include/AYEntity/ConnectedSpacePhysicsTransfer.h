#pragma once
#include <AYEntity/ConnectedSpacePhysicsRoutes.h>
#include <AYPhysics/PhysicsTypes.h>
#include <functional>
#include <optional>

namespace ayt::entity {
class Entity;
}
namespace ayt::physics { class PhysicsWorld3D; }

namespace ayt::entity {
using SpaceBodyRouteResolver = std::function<std::optional<SpaceBodyRoute>(SpaceRegionId)>;

/// Borrow one route table for body and character transfers. Keep routes alive
/// while the returned function is used.
inline SpaceBodyRouteResolver makeSpaceBodyRouteResolver(const SpacePhysicsRoutes& routes) {
    return [&routes](SpaceRegionId region) { return routes.resolve(region); };
}

/// Cross a connected opening from the latest completed physics snapshot.
/// Call on the game thread after FixedPostPhysics and before the next step.
/// The body must be a root, dynamic, 3D PhysicsToEntity Spatial Entity.
/// Its existing collision layer/mask must already isolate the source region;
/// configure SpacePhysicsRoutes on EntityPhysicsBridge before creation or
/// establish an equivalent source route for a manually owned body.
/// One queued command changes pose, linear/angular velocities, layer and mask
/// together. Queue rejection leaves Entity, output and body unchanged. Ok is
/// queue admission; the physical snapshot catches up after stepAndWait().
/// A transfer is a teleport, not a swept collision across the entrance.
physics::PhysResult transferSpatialRigidBodyAcrossSpaces(Entity& entity,
    const physics::BodyTransform& currentBody,
    const math::FVector3& displacement, uint32_t maxHops,
    physics::PhysicsWorld3D& physicsWorld,
    const SpaceBodyRouteResolver& route,
    SpaceTraversal& output);

/// Transfer a gameplay-owned 3D character at a completed-step boundary.
/// currentState must be the latest live controller snapshot. One queue command
/// changes feet, velocity, up and collision route; admission rejection leaves
/// the Entity and output unchanged. The first destination step retains mapped
/// velocity/up; gameplay then submits destination input/gravity and syncs
/// subsequent controller snapshots to Entity.
physics::PhysResult transferSpatialCharacterAcrossSpaces(Entity& entity,
    const physics::CharacterController3DState& currentState,
    const math::FVector3& displacement, uint32_t maxHops,
    physics::PhysicsWorld3D& physicsWorld,
    const SpaceBodyRouteResolver& route,
    SpaceTraversal& output);
} // namespace ayt::entity
