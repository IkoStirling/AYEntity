#include <AYEntity/ConnectedSpacePhysicsQuery.h>
#include <AYEntity/World.h>

namespace ayt::entity {
SpacePhysicsTrace raycastConnectedSpaces(const World& world, SpaceRegionId source,
    const math::Ray& ray, float maxDistance, uint32_t maxHops,
    const SpacePhysicsResolver& resolve) {
    SpacePhysicsTrace result;
    if (!resolve) {
        result.path.status = SpaceTraceStatus::QueryFailed;
        result.physicsResult = physics::PhysResult::InvalidParam;
        return result;
    }
    result.path = world.connectedSpaces().traceRay(source, ray, maxDistance, maxHops,
        [&](SpaceRegionId region, const math::Ray& localRay, float limit)
            -> std::optional<SpaceLocalHit> {
            const auto target = resolve(region);
            if (!target || !target->query || !target->query->isReady()) {
                result.physicsResult = physics::PhysResult::InvalidState;
                return std::nullopt;
            }
            physics::RaycastHit local;
            const auto status = target->query->raycastSync(localRay, local, target->layerMask);
            if (status != physics::PhysResult::Ok) {
                result.physicsResult = status;
                return std::nullopt;
            }
            if (local.hit && local.distance <= limit) {
                result.localHit = local;
                return SpaceLocalHit{true, local.distance};
            }
            return SpaceLocalHit{};
        });
    if (result.path.status == SpaceTraceStatus::QueryFailed
        && result.physicsResult == physics::PhysResult::Ok)
        result.physicsResult = physics::PhysResult::InvalidParam;
    if (result.path.status != SpaceTraceStatus::Hit) result.localHit = {};
    return result;
}
} // namespace ayt::entity
