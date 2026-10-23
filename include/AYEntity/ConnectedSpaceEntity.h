#pragma once
#include <AYEntity/ConnectedSpaces.h>

namespace ayt::entity {
class Entity;

/// Applies one connected-space displacement to a root Spatial Entity and
/// changes its intrinsic region atomically with its pose. This presentation/
/// kinematic entry rejects physics-authoritative rigid bodies; use
/// transferSpatialRigidBodyAcrossSpaces at a fixed-step boundary for those.
/// The velocity argument is updated only on success. A hop limit stops the
/// entity on the entrance plane and is reported in output.
bool moveSpatialEntityAcrossSpaces(Entity& entity,
    const math::FVector3& displacement, math::FVector3& velocity,
    uint32_t maxHops, SpaceTraversal& output);
} // namespace ayt::entity
