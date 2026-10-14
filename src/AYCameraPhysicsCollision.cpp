#include <AYEntity.h>
#include <AYEntity/CameraRuntime.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/components/CameraControllerComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include <AYPhysics/PhysicsSubSystem.h>
#include <limits>

namespace ayt::entity {
void installCameraPhysicsCollision(World& world, ayt::physics::PhysicsManager* manager) {
    cameraRuntime(world).setDefaultCollisionQuery([manager](const CameraCollisionProbe& probe) {
        auto* service = manager;
        if (!service) if (auto* subsystem = ayt::physics::PhysicsSubSystem::findRegistered()) service = subsystem->manager();
        if (!service || !service->isRunning() || !service->world3D()) return std::numeric_limits<float>::quiet_NaN();
        ayt::physics::SphereSweep query;
        query.origin = probe.anchor; query.displacement = probe.desiredEye - probe.anchor;
        query.radius = std::max(.0001f,probe.radius); query.layerMask = probe.layerMask;
        const auto ignore = [&](const Entity* entity) {
            if (!entity) return;
            auto* body = const_cast<Entity*>(entity)->getComponent<RigidBodyComponent>();
            if (body && body->getPhysicsDimension() == RigidBodyComponent::PhysicsDimension::ThreeD && body->getBodyHandle()
                && std::find(query.ignoredBodies.begin(),query.ignoredBodies.end(),body->getBodyHandle()) == query.ignoredBodies.end())
                query.ignoredBodies.push_back(body->getBodyHandle());
        };
        ignore(probe.cameraEntity);
        if (probe.ignoreTarget && probe.targetEntity) {
            ignore(probe.targetEntity);
            for (auto* owner = probe.targetEntity->owner(); owner; owner = owner->owner()) ignore(owner);
            std::vector<const Entity*> pending{probe.targetEntity};
            while (!pending.empty()) {
                const auto* entity = pending.back(); pending.pop_back();
                for (auto* child : entity->ownedEntities()) { ignore(child); pending.push_back(child); }
                if (query.ignoredBodies.size() > 64) return std::numeric_limits<float>::quiet_NaN();
            }
        }
        ayt::physics::RaycastHit hit;
        if (service->world3D()->sphereSweepSync(query,hit) != ayt::physics::PhysResult::Ok)
            return std::numeric_limits<float>::quiet_NaN();
        return hit.hit ? hit.distance : query.displacement.length();
    });
}
} // namespace ayt::entity
