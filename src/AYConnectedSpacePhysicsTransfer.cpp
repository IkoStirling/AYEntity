#include <AYEntity/ConnectedSpacePhysicsTransfer.h>
#include <AYEntity.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYPhysics/PhysicsWorld3D.h>

namespace ayt::entity {
physics::PhysResult transferSpatialRigidBodyAcrossSpaces(Entity& entity,
    const physics::BodyTransform& currentBody,
    const math::FVector3& displacement, uint32_t maxHops,
    physics::PhysicsWorld3D& physicsWorld,
    const SpaceBodyRouteResolver& route, SpaceTraversal& output) {
    auto* world = entity.getWorld();
    auto* body = entity.getComponent<RigidBodyComponent>();
    if (!world || !entity.isSpatial() || entity.spatialParent() || !entity.spaceRegion()
        || !body || body->getSyncMode()!=RigidBodyComponent::SyncMode::PhysicsToEntity
        || body->getPhysicsDimension()!=RigidBodyComponent::PhysicsDimension::ThreeD
        || body->isStatic() || body->isKinematic()
        || currentBody.body==physics::InvalidBodyHandle
        || currentBody.body!=body->getBodyHandle()
        || currentBody.dimension!=physics::PhysicsDimension::ThreeD || !route)
        return physics::PhysResult::InvalidParam;
    SpaceTraversal candidate;
    if (!world->connectedSpaces().travel({entity.spaceRegion(),
            {currentBody.position,currentBody.rotation},currentBody.linearVelocity},
            displacement,maxHops,candidate) || candidate.crossings==0)
        return physics::PhysResult::InvalidParam;
    const auto destinationRoute = route(candidate.state.region);
    if (!destinationRoute || destinationRoute->layer>=32)
        return physics::PhysResult::InvalidParam;
    const auto angularRotation = candidate.state.pose.rotation*currentBody.rotation.inverse();
    const auto angularVelocity = angularRotation*currentBody.angularVelocity;
    const auto result = physicsWorld.transferRigidbody(currentBody.body,
        candidate.state.pose.position,candidate.state.pose.rotation,
        candidate.state.velocity,angularVelocity,
        destinationRoute->layer,destinationRoute->collideMask);
    if (result!=physics::PhysResult::Ok) return result;
    // For a validated root Spatial Entity, these setters cannot fail. The
    // component pose is presentation state until the next completed snapshot.
    (void)entity.setWorldPose(candidate.state.pose.position,candidate.state.pose.rotation);
    (void)entity.setSpaceRegion(candidate.state.region);
    body->setVelocity(candidate.state.velocity.x,candidate.state.velocity.y,
                      candidate.state.velocity.z);
    output=candidate;
    return result;
}
} // namespace ayt::entity
