#pragma once
#include <AYEntity/ConnectedSpaces.h>
#include <AYPhysics/PhysicsTypes.h>
#include <functional>
#include <optional>

namespace ayt::entity {
class Entity;
}
namespace ayt::physics { class PhysicsWorld3D; }

namespace ayt::entity {
/// One shared Jolt world can isolate at most 32 regions with disjoint layers.
/// The route maps a destination region to its object layer and collide mask.
struct SpaceBodyRoute {
    physics::PhysLayer layer = 0;
    physics::PhysLayerMask collideMask = 0;
};
using SpaceBodyRouteResolver = std::function<std::optional<SpaceBodyRoute>(SpaceRegionId)>;

/// Cross a connected opening from the latest completed physics snapshot.
/// Call on the game thread after FixedPostPhysics and before the next step.
/// The body must be a root, dynamic, 3D PhysicsToEntity Spatial Entity.
/// Its existing collision layer/mask must already isolate the source region;
/// the default EntityPhysicsBridge body creation does not assign region layers.
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
} // namespace ayt::entity
