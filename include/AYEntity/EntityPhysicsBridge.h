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
#include <AYEntity/ConnectedSpacePhysicsRoutes.h>

#include <AYPhysics/PhysicsTypes.h>
#include <AYPhysics/GravityField3D.h>
#include <AYPhysics/CharacterController3D.h>

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

    /// Borrow shared-world routes for nonzero Spatial regions. When configured,
    /// creation and later region changes use the same route as portal queries
    /// and transfers. Missing routes reject creation; zero-region bodies keep
    /// their legacy layer. The caller keeps routes alive through shutdown().
    void setSpacePhysicsRoutes(const SpacePhysicsRoutes* routes) noexcept {
        _spacePhysicsRoutes = routes;
    }

    void syncEntityToPhysics();  // FixedPrePhysics
    void syncPhysicsToEntity();  // FixedPostPhysics

    /// Submit character movement with this Spatial Entity's gravity response.
    /// Call after the bridge's FixedPrePhysics sync and before the physics step.
    /// The caller still owns character creation and movement intent.
    ayt::physics::PhysResult setCharacterInputForEntity(
        const Entity& entity, ayt::physics::CharacterHandle character,
        ayt::physics::CharacterController3DInput input);

    /// Create a gameplay-owned controller with this Entity's region filter.
    /// The caller supplies feet/up and destroys the returned handle. A missing
    /// nonzero-region route rejects creation without publishing a handle.
    ayt::physics::PhysResult createCharacterForEntity(
        const Entity& entity, ayt::physics::CharacterController3DDesc desc,
        ayt::physics::CharacterHandle& outHandle);

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
        SpaceRegionId routedRegion = 0;
        SpaceBodyRoute routedBody{};
        bool routeSynced = false;

        // R10 runtime-parameter sync: values last pushed as incremental
        // commands. paramsSynced=false (fresh binding) baselines from the
        // component without re-sending — the create descriptor already
        // carried these values. Field diffs after that push one-shot
        // SetMass / SetMaterial / SetGravityScale / SetRigidbodyVelocity.
        bool paramsSynced = false;
        float lastSyncedMass = 0.0f;
        float lastSyncedFriction = 0.0f;
        float lastSyncedRestitution = 0.0f;
        float lastSyncedGravityScale = 0.0f;
        float lastSyncedVelocity[3] = {0.0f, 0.0f, 0.0f};
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
    // R10: diff component runtime params (mass / friction / restitution /
    // gravityScale / velocity) against the last-pushed baseline and emit
    // one-shot incremental commands.
    void syncRuntimeParams(PhysicsBinding& binding,
                           uint32_t& rejectedCommands,
                           ayt::physics::PhysResult& firstFailure);
    // R10: flush accumulated entity force / torque as per-frame commands
    // (cleared only when pushed successfully).
    void flushEntityForces(PhysicsBinding& binding,
                           uint32_t& rejectedCommands,
                           ayt::physics::PhysResult& firstFailure);
    // Destroy every owned body + its colliders (manager handoff path).
    void destroyOwnedBodies();

    void removeBodyLookup(uint32_t entityId, const PhysicsBinding& binding);

    std::unordered_map<uint32_t, PhysicsBinding> _bindingsByEntity;
    std::unordered_map<uint64_t, uint32_t> _entityByBody;
    ayt::physics::PhysicsManager* _managerOverride = nullptr;
    ayt::physics::PhysicsManager* _boundPhysicsManager = nullptr;
    ayt::physics::GravityField3D _gravityField;
    const SpacePhysicsRoutes* _spacePhysicsRoutes = nullptr;
    uint64_t _bindingEpoch = 0;
};

} // namespace ayt::entity
