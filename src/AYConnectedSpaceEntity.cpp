#include <AYEntity/ConnectedSpaceEntity.h>
#include <AYEntity.h>
#include <AYEntity/components/RigidBodyComponent.h>

namespace ayt::entity {
bool moveSpatialEntityAcrossSpaces(Entity& entity, const math::FVector3& displacement,
                                   math::FVector3& velocity, uint32_t maxHops,
                                   SpaceTraversal& output) {
    auto* world = entity.getWorld();
    if (!world || !entity.isSpatial() || entity.spatialParent() || !entity.spaceRegion())
        return false;
    if (auto* body = entity.getComponent<RigidBodyComponent>();
        body && body->getSyncMode() == RigidBodyComponent::SyncMode::PhysicsToEntity)
        return false;
    math::FVector3 position;
    math::FQuaternion rotation;
    if (!entity.worldPose(position, rotation)) return false;
    SpaceTraversal candidate;
    if (!world->connectedSpaces().travel({entity.spaceRegion(), {position, rotation}, velocity},
                                         displacement, maxHops, candidate)) return false;
    if (!entity.setWorldPose(candidate.state.pose.position,candidate.state.pose.rotation)) return false;
    // travel guarantees that every destination still belongs to this graph.
    if (!entity.setSpaceRegion(candidate.state.region)) return false;
    velocity = candidate.state.velocity;
    output = candidate;
    return true;
}
} // namespace ayt::entity
