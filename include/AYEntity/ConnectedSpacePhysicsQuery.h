#pragma once
#include <AYEntity/ConnectedSpaces.h>
#include <AYPhysics/IPhysicsQuery.h>
#include <functional>
#include <optional>

namespace ayt::entity {
class World;

struct SpacePhysicsRegion {
    physics::IPhysicsQuery* query = nullptr; // borrowed for this call
    physics::PhysLayerMask layerMask = 0xFFFFFFFFu;
};
using SpacePhysicsResolver = std::function<std::optional<SpacePhysicsRegion>(SpaceRegionId)>;

struct SpacePhysicsTrace {
    SpaceTrace path{};
    physics::RaycastHit localHit{}; // valid only when path.status == Hit
    physics::PhysResult physicsResult = physics::PhysResult::Ok;
};

/// Route a 3D physics ray through World::connectedSpaces(). The resolver must
/// supply a region-isolated query world or disjoint layer mask for every region;
/// a shared unfiltered Jolt world can report bodies behind the wrong portal.
/// No-hit, query failure and exhausted portal budget remain distinct.
SpacePhysicsTrace raycastConnectedSpaces(const World& world, SpaceRegionId source,
    const math::Ray& ray, float maxDistance, uint32_t maxHops,
    const SpacePhysicsResolver& resolve);
} // namespace ayt::entity
