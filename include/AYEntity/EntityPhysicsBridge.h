#pragma once
// AYEntity/EntityPhysicsBridge.h - Entity<->Physics parameter + pose bridge.
//
// Owns the binding tables that turn ECS RigidBodyComponent / ColliderComponent
// data into AYPhysics bodies and collider shapes, and push AYPhysics
// snapshots back into entity Transforms:
//
//   FixedPrePhysics  syncEntityToPhysics:
//     - creates the body from component fields (mass/type/velocity/friction/
//       restitution/pose) when RigidBodyComponent has no body handle yet
//     - creates colliders from ColliderComponent::shapes (1 shape = 1
//       createCollider); rebuilds them on revision bump / direct vector edits
//     - submits entity pose to the body for EntityToPhysics entities
//   FixedPostPhysics syncPhysicsToEntity:
//     - applies the physics snapshot (position/rotation/velocity) for
//       PhysicsToEntity entities
//
// Lifecycle: bodies created by this bridge (ownsBody) are destroyed when the
// binding is swept (entity gone / component removed) or the bound manager
// changes. Pre-existing handles (scene-authored) are adopted, not owned.
// shutdown() only drops tables — the PhysicsManager owns backend teardown.
//
// Manager resolution: PhysicsSubSystem::findRegistered() by default;
// setPhysicsManager() overrides it (tests / embedded hosts).
//
// NOTE: this header includes AYPhysics public headers. Consumers must link
// AYPhysics (AYEntity itself links it PRIVATE).

#include <AYEntity/components/ColliderComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>

#include <AYPhysics/PhysicsTypes.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ayt::entity {

class Entity;

} // namespace ayt::entity

namespace ayt::physics {
class PhysicsManager;
class PhysicsSubSystem;
class PhysicsWorld2D;
class PhysicsWorld3D;
} // namespace ayt::physics

namespace ayt::entity {

class EntityPhysicsBridge {
public:
    EntityPhysicsBridge() = default;
    ~EntityPhysicsBridge() { shutdown(); }

    // Test / embedded-host override. nullptr restores auto-lookup via
    // PhysicsSubSystem::findRegistered().
    void setPhysicsManager(ayt::physics::PhysicsManager* mgr);
    ayt::physics::PhysicsManager* physicsManager() const;

    void syncEntityToPhysics();  // FixedPrePhysics
    void syncPhysicsToEntity();  // FixedPostPhysics

    // Drop all bindings WITHOUT destroying bodies (the PhysicsManager's own
    // shutdown tears down its backend world, which frees every body).
    void shutdown();

private:
    struct PhysicsBinding {
        Entity* entity = nullptr;
        Transform* transform = nullptr;
        RigidBodyComponent* rigidBody = nullptr;
        ColliderComponent* collider = nullptr;
        ayt::physics::BodyHandle body = ayt::physics::InvalidBodyHandle;
        RigidBodyComponent::PhysicsDimension dimension =
            RigidBodyComponent::PhysicsDimension::ThreeD;
        RigidBodyComponent::SyncMode syncMode = RigidBodyComponent::SyncMode::None;
        math::FVector3 submittedPosition{};
        math::FQuaternion submittedRotation{};
        uint64_t lastSyncedRevision = 0;
        uint64_t seenEpoch = 0;
        bool hasSubmittedPose = false;
        // Collider state — colliderHandles parallel ColliderComponent::shapes
        // entries while a collider is bound.
        std::vector<ayt::physics::ColliderHandle> colliderHandles;
        uint64_t lastSyncedColliderRevision = 0;
        std::vector<ColliderShapeSpec> lastSyncedShapes;
        bool ownsBody = false;  // body created by this bridge -> destroy on teardown
    };

    ayt::physics::PhysicsManager* resolveManager() const;

    static uint64_t bodyKey(ayt::physics::BodyHandle body,
                            RigidBodyComponent::PhysicsDimension dimension) noexcept;
    static uint64_t bodyKey(const ayt::physics::BodyTransform& body) noexcept;
    static bool poseEquals(const PhysicsBinding& binding,
                           const Transform& transform) noexcept;
    static bool shapesEqual(const std::vector<ColliderShapeSpec>& a,
                            const std::vector<ColliderShapeSpec>& b) noexcept;

    static void buildRigidbodyDesc(ayt::physics::RigidbodyDesc& desc,
                                   const Transform& transform,
                                   const RigidBodyComponent& rigidBody) noexcept;
    // Returns false for unknown shape kinds (spec skipped, warn emitted).
    static bool buildColliderDesc(ayt::physics::ColliderDesc& desc,
                                  const ColliderShapeSpec& spec) noexcept;

    ayt::physics::PhysResult createBodyOn(
        const ayt::physics::RigidbodyDesc& desc,
        RigidBodyComponent::PhysicsDimension dimension,
        ayt::physics::BodyHandle& outHandle) const;
    ayt::physics::PhysResult createColliderOn(
        const ayt::physics::ColliderDesc& desc,
        RigidBodyComponent::PhysicsDimension dimension,
        ayt::physics::ColliderHandle& outHandle) const;
    void destroyBody(ayt::physics::BodyHandle body,
                     RigidBodyComponent::PhysicsDimension dimension) const;
    void releaseColliders(PhysicsBinding& binding);

    // Create/rebuild colliders for a binding. Caller must have a valid body.
    void syncColliders(PhysicsBinding& binding,
                       Entity* entity,
                       uint32_t& rejectedCommands,
                       ayt::physics::PhysResult& firstFailure);
    // Destroy every owned body + its colliders (manager handoff path).
    void destroyOwnedBodies();

    void removeBodyLookup(uint32_t entityId, const PhysicsBinding& binding);

    std::unordered_map<uint32_t, PhysicsBinding> _bindingsByEntity;
    std::unordered_map<uint64_t, uint32_t> _entityByBody;
    ayt::physics::PhysicsManager* _managerOverride = nullptr;
    ayt::physics::PhysicsManager* _boundPhysicsManager = nullptr;
    uint64_t _bindingEpoch = 0;
};

} // namespace ayt::entity
