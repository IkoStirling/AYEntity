// EntityPhysicsBridgeTest.cpp - EntityPhysicsBridge integration tests.
//
// Drives the bridge against a live PhysicsManager (Mock 3D backend for
// command/payload capture via testaccess, Null for behavior-only cases).
// Covers the parameter bridge (component fields -> RigidbodyDesc) and the
// collider lifecycle (create / revision rebuild / direct-write fallback /
// destroy-on-entity-gone / adopt-don't-own).
//
// Mock backend contract (AYPhysics): create payloads are captured by the
// physics thread during drain; stepAndWait is the completion barrier, so all
// commands pushed before it are executed once it returns (FIFO ring).

#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityPhysicsBridge.h>
#include <AYEntity/components/ColliderComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>

#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsBackendTestAccess.h>

#include <AYTest.h>

#include <cstring>

using namespace ayt::entity;

namespace {

constexpr float kStep = 1.0f / 60.0f;

// Fresh manager + bridge + World for one test. Caller owns teardown order:
// destroy entities, mgr->shutdown(), World::shutdown().
struct TestRig {
    std::unique_ptr<ayt::physics::PhysicsManager> manager;
    EntityPhysicsBridge bridge;

    explicit TestRig(const ayt::physics::PhysicsBackendDescriptor& desc) {
        World::instance().initialize();
        registerEntityComponents();
        manager = ayt::physics::PhysicsManager::create(desc);
        bridge.setPhysicsManager(manager.get());
    }

    void step() {
        manager->stepAndWait(kStep, ayt::time::Duration::fromSeconds(1));
    }
};

bool hasCommand(const std::vector<ayt::physics::PhysicsCommand>& cmds,
                ayt::physics::PhysicsCommandType type) {
    for (const auto& cmd : cmds) {
        if (cmd.type == type) return true;
    }
    return false;
}

size_t countCommands(const std::vector<ayt::physics::PhysicsCommand>& cmds,
                     ayt::physics::PhysicsCommandType type) {
    size_t n = 0;
    for (const auto& cmd : cmds) {
        if (cmd.type == type) ++n;
    }
    return n;
}

} // namespace

TEST_SUITE(EntityPhysicsBridge)

// =============================================================================
// Body creation (parameter bridge)
// =============================================================================

TEST_CASE(creates_body_from_component_params)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    auto* t = e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    rb->setMass(3.5f);
    rb->setFriction(0.8f);
    rb->setRestitution(0.1f);
    rb->setVelocity(1.0f, 2.0f, 3.0f);
    t->setPosition(5.0f, 6.0f, 7.0f);

    rig.bridge.syncEntityToPhysics();
    CHECK(rb->getBodyHandle() != 0u);  // handle minted + written back

    rig.step();
    const auto& payloads = ayt::physics::testaccess::mockBackendCreatePayloads();
    CHECK(payloads.size() >= 1u);
    const auto& p = payloads[0];
    CHECK(p.kind == ayt::physics::PhysicsCreatePayload::Kind::Rigidbody);
    CHECK(p.rigidDesc.type == ayt::physics::BodyType::Dynamic);
    CHECK_FLOAT_EQ(p.rigidDesc.mass, 3.5f, 0.0001f);
    CHECK_FLOAT_EQ(p.rigidDesc.material.friction, 0.8f, 0.0001f);
    CHECK_FLOAT_EQ(p.rigidDesc.material.restitution, 0.1f, 0.0001f);
    CHECK_FLOAT_EQ(p.rigidDesc.linearVelocity.x, 1.0f, 0.0001f);
    CHECK_FLOAT_EQ(p.rigidDesc.linearVelocity.y, 2.0f, 0.0001f);
    CHECK_FLOAT_EQ(p.rigidDesc.linearVelocity.z, 3.0f, 0.0001f);
    CHECK_FLOAT_EQ(p.rigidDesc.position.x, 5.0f, 0.0001f);
    CHECK_FLOAT_EQ(p.rigidDesc.position.y, 6.0f, 0.0001f);
    CHECK_FLOAT_EQ(p.rigidDesc.position.z, 7.0f, 0.0001f);

    World::instance().destroyEntity(e);
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(static_kinematic_map_to_body_types)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    rb->setStatic(true);

    rig.bridge.syncEntityToPhysics();
    rig.step();
    const auto& payloads = ayt::physics::testaccess::mockBackendCreatePayloads();
    CHECK(payloads.size() >= 1u);
    CHECK(payloads[0].rigidDesc.type == ayt::physics::BodyType::Static);

    World::instance().destroyEntity(e);
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(sync_mode_none_creates_no_body)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setSyncMode(RigidBodyComponent::SyncMode::None);

    rig.bridge.syncEntityToPhysics();
    rig.step();
    CHECK(rb->getBodyHandle() == 0u);
    CHECK_FALSE(hasCommand(ayt::physics::testaccess::mockBackendCommands(),
                           ayt::physics::PhysicsCommandType::CreateRigidbody));

    World::instance().destroyEntity(e);
    rig.manager->shutdown();
    World::instance().shutdown();
}

// =============================================================================
// Collider creation / rebuild
// =============================================================================

TEST_CASE(creates_colliders_from_shapes)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    e->addComponent<RigidBodyComponent>();
    auto* collider = e->addComponent<ColliderComponent>();

    auto& box = collider->addShape();
    box.halfExtents = ayt::math::FVector3(1.0f, 2.0f, 3.0f);
    box.friction = 0.7f;

    auto& cap = collider->addShape();
    cap.shape = ColliderShapeType::Capsule;
    cap.radius = 0.4f;
    cap.height = 1.6f;
    cap.isTrigger = true;

    rig.bridge.syncEntityToPhysics();
    rig.step();

    const auto& payloads = ayt::physics::testaccess::mockBackendCreatePayloads();
    CHECK(payloads.size() >= 3u);  // 1 rigidbody + 2 colliders
    CHECK(payloads[0].kind == ayt::physics::PhysicsCreatePayload::Kind::Rigidbody);
    CHECK(payloads[1].kind == ayt::physics::PhysicsCreatePayload::Kind::Collider);
    CHECK(payloads[1].colliderDesc.shape == ayt::physics::ColliderShape::Box);
    CHECK_FLOAT_EQ(payloads[1].colliderDesc.halfExtents.x, 1.0f, 0.0001f);
    CHECK_FLOAT_EQ(payloads[1].colliderDesc.halfExtents.y, 2.0f, 0.0001f);
    CHECK_FLOAT_EQ(payloads[1].colliderDesc.halfExtents.z, 3.0f, 0.0001f);
    CHECK_FLOAT_EQ(payloads[1].colliderDesc.material.friction, 0.7f, 0.0001f);
    CHECK(payloads[2].colliderDesc.shape == ayt::physics::ColliderShape::Capsule);
    CHECK_FLOAT_EQ(payloads[2].colliderDesc.radius, 0.4f, 0.0001f);
    CHECK_FLOAT_EQ(payloads[2].colliderDesc.height, 1.6f, 0.0001f);
    CHECK(payloads[2].colliderDesc.isTrigger);

    // Idempotent: a second sync with no changes issues no new commands.
    const size_t collidersBefore =
        countCommands(ayt::physics::testaccess::mockBackendCommands(),
                      ayt::physics::PhysicsCommandType::CreateCollider);
    rig.bridge.syncEntityToPhysics();
    rig.step();
    const size_t collidersAfter =
        countCommands(ayt::physics::testaccess::mockBackendCommands(),
                      ayt::physics::PhysicsCommandType::CreateCollider);
    CHECK_INT_EQ(static_cast<int>(collidersAfter - collidersBefore), 0);

    World::instance().destroyEntity(e);
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(rebuilds_colliders_on_revision_bump)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    e->addComponent<RigidBodyComponent>();
    auto* collider = e->addComponent<ColliderComponent>();
    collider->addShape();

    rig.bridge.syncEntityToPhysics();
    rig.step();

    collider->addShape();  // revision 1 -> 2: rebuild expected
    rig.bridge.syncEntityToPhysics();
    rig.step();

    const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
    CHECK_INT_EQ(static_cast<int>(countCommands(
                     cmds, ayt::physics::PhysicsCommandType::DestroyCollider)),
                 1);  // old single shape released
    CHECK_INT_EQ(static_cast<int>(countCommands(
                     cmds, ayt::physics::PhysicsCommandType::CreateCollider)),
                 3);  // 1 initial + 2 rebuilt
    // Old handles are never re-destroyed after the rebuild is a no-op.
    const size_t destroysBefore = countCommands(
        cmds, ayt::physics::PhysicsCommandType::DestroyCollider);
    rig.bridge.syncEntityToPhysics();
    rig.step();
    CHECK_INT_EQ(static_cast<int>(countCommands(
                     ayt::physics::testaccess::mockBackendCommands(),
                     ayt::physics::PhysicsCommandType::DestroyCollider)
                 - destroysBefore),
                 0);

    World::instance().destroyEntity(e);
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(rebuilds_colliders_on_direct_vector_write)
{
    // Direct edits bypass the revision counter; the bridge's deep-compare
    // fallback must still detect the change.
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    e->addComponent<RigidBodyComponent>();
    auto* collider = e->addComponent<ColliderComponent>();
    collider->addShape();

    rig.bridge.syncEntityToPhysics();
    rig.step();

    const uint32_t revisionBefore = collider->revision;
    collider->shapes[0].halfExtents = ayt::math::FVector3(9.0f, 9.0f, 9.0f);  // no bump
    collider->shapes.push_back(ColliderShapeSpec());                            // no bump
    CHECK_INT_EQ(static_cast<int>(collider->revision), static_cast<int>(revisionBefore));

    rig.bridge.syncEntityToPhysics();
    rig.step();

    const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
    CHECK_INT_EQ(static_cast<int>(countCommands(
                     cmds, ayt::physics::PhysicsCommandType::DestroyCollider)),
                 1);
    CHECK_INT_EQ(static_cast<int>(countCommands(
                     cmds, ayt::physics::PhysicsCommandType::CreateCollider)),
                 3);  // 1 initial + 2 rebuilt (with the new halfExtents)
    const auto& payloads = ayt::physics::testaccess::mockBackendCreatePayloads();
    CHECK(payloads.size() >= 4u);
    // Rebuild order follows shapes[]: shapes[0] (edited, 9,9,9) then
    // shapes[1] (default) → the edited spec lands at index 2.
    CHECK_FLOAT_EQ(payloads[2].colliderDesc.halfExtents.x, 9.0f, 0.0001f);

    World::instance().destroyEntity(e);
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(empty_shape_list_creates_no_colliders)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    e->addComponent<RigidBodyComponent>();
    e->addComponent<ColliderComponent>();  // no shapes

    rig.bridge.syncEntityToPhysics();
    rig.step();

    CHECK_FALSE(hasCommand(ayt::physics::testaccess::mockBackendCommands(),
                           ayt::physics::PhysicsCommandType::CreateCollider));

    World::instance().destroyEntity(e);
    rig.manager->shutdown();
    World::instance().shutdown();
}

// =============================================================================
// Lifecycle
// =============================================================================

TEST_CASE(destroys_owned_body_and_colliders_on_entity_destroy)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    e->addComponent<RigidBodyComponent>();
    auto* collider = e->addComponent<ColliderComponent>();
    collider->addShape();

    rig.bridge.syncEntityToPhysics();
    rig.step();
    CHECK(e->getComponent<RigidBodyComponent>()->getBodyHandle() != 0u);

    World::instance().destroyEntity(e);
    rig.bridge.syncEntityToPhysics();  // epoch sweep
    rig.step();

    const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
    CHECK(hasCommand(cmds, ayt::physics::PhysicsCommandType::DestroyRigidbody));
    CHECK(hasCommand(cmds, ayt::physics::PhysicsCommandType::DestroyCollider));

    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(adopts_preexisting_body_handle_without_owning)
{
    // Scene-authored handles (setBodyHandle externally) are adopted: the
    // bridge neither creates nor destroys them.
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setBodyHandle(0x12345678u);

    rig.bridge.syncEntityToPhysics();
    rig.step();
    CHECK_FALSE(hasCommand(ayt::physics::testaccess::mockBackendCommands(),
                           ayt::physics::PhysicsCommandType::CreateRigidbody));

    World::instance().destroyEntity(e);
    rig.bridge.syncEntityToPhysics();
    rig.step();
    CHECK_FALSE(hasCommand(ayt::physics::testaccess::mockBackendCommands(),
                           ayt::physics::PhysicsCommandType::DestroyRigidbody));

    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(bridge_2d_body_and_collider_creation)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Null;
    desc.kind2D = ayt::physics::BackendKind::Null;
    TestRig rig(desc);

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setPhysicsDimension(RigidBodyComponent::PhysicsDimension::TwoD);
    auto* collider = e->addComponent<ColliderComponent>();
    collider->addShape();

    rig.bridge.syncEntityToPhysics();
    CHECK(rb->getBodyHandle() != 0u);
    rig.step();  // must not crash; empty snapshot path exercised below too

    rig.bridge.syncPhysicsToEntity();

    World::instance().destroyEntity(e);
    rig.bridge.syncEntityToPhysics();  // sweep destroys the 2D body
    rig.manager->shutdown();
    World::instance().shutdown();
}

// =============================================================================
// R10 — runtime parameter sync (incremental commands) + force/torque flush
// =============================================================================

TEST_CASE(runtime_param_diffs_emit_incremental_commands)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);
    ayt::physics::testaccess::reset();

    Entity* e = World::instance().createEntity();
    auto* t = e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    t->setPosition(0.0f, 5.0f, 0.0f);
    rb->setMass(2.0f);
    rb->setFriction(0.5f);
    rb->setRestitution(0.3f);
    rb->setGravityScale(1.0f);
    rb->setVelocity(0.0f, 0.0f, 0.0f);

    rig.bridge.syncEntityToPhysics();
    rig.step();
    ayt::physics::testaccess::reset();  // drop the create-path commands

    // Diff every runtime param in one tick -> one-shot commands.
    rb->setMass(4.0f);
    rb->setGravityScale(0.25f);
    rb->setVelocity(1.0f, 0.0f, 0.0f);
    rig.bridge.syncEntityToPhysics();
    rig.step();
    {
        const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetMass) == 1u);
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetGravityScale) == 1u);
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetRigidbodyVelocity) == 1u);
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetMaterial) == 0u);
    }

    // Friction/restitution diff needs a bound collider: add one, then change.
    auto* collider = e->addComponent<ColliderComponent>();
    collider->addShape();  // default Box
    rig.bridge.syncEntityToPhysics();
    rig.step();
    rb->setFriction(0.9f);
    rb->setRestitution(0.05f);
    rig.bridge.syncEntityToPhysics();
    rig.step();
    {
        const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetMaterial) == 1u);
    }

    World::instance().destroyEntity(e);
    rig.bridge.syncEntityToPhysics();
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(unchanged_runtime_params_resend_nothing)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);
    ayt::physics::testaccess::reset();

    Entity* e = World::instance().createEntity();
    auto* t = e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    t->setPosition(0.0f, 5.0f, 0.0f);
    rb->setMass(2.0f);

    rig.bridge.syncEntityToPhysics();
    rig.step();
    ayt::physics::testaccess::reset();

    // Two quiescent ticks: no runtime commands, ever.
    rig.bridge.syncEntityToPhysics();
    rig.step();
    rig.bridge.syncEntityToPhysics();
    rig.step();
    const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
    CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetMass) == 0u);
    CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetGravityScale) == 0u);
    CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetRigidbodyVelocity) == 0u);
    CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::SetMaterial) == 0u);
    CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::ApplyForce) == 0u);
    CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::ApplyTorque) == 0u);

    World::instance().destroyEntity(e);
    rig.bridge.syncEntityToPhysics();
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(entity_force_and_torque_flush_as_commands)
{
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);
    ayt::physics::testaccess::reset();

    Entity* e = World::instance().createEntity();
    auto* t = e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    t->setPosition(0.0f, 5.0f, 0.0f);
    rb->setMass(2.0f);

    rig.bridge.syncEntityToPhysics();
    rig.step();
    ayt::physics::testaccess::reset();

    // Accumulate + flush in one tick.
    rb->applyForce(10.0f, 0.0f, 0.0f);
    rb->applyTorque(0.0f, 0.0f, 5.0f);
    rig.bridge.syncEntityToPhysics();
    rig.step();
    {
        const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::ApplyForce) == 1u);
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::ApplyTorque) == 1u);
        // Command payload carries the vector.
        bool sawForce = false, sawTorque = false;
        for (const auto& cmd : cmds) {
            if (cmd.type == ayt::physics::PhysicsCommandType::ApplyForce
                && cmd.body == rb->getBodyHandle()
                && cmd.u.vec4.x == 10.0f && cmd.u.vec4.y == 0.0f) {
                sawForce = true;
            }
            if (cmd.type == ayt::physics::PhysicsCommandType::ApplyTorque
                && cmd.u.vec4.z == 5.0f) {
                sawTorque = true;
            }
        }
        CHECK(sawForce);
        CHECK(sawTorque);
    }

    // Flushed = cleared: the next tick must not re-send.
    rig.bridge.syncEntityToPhysics();
    rig.step();
    {
        const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::ApplyForce) == 1u);
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::ApplyTorque) == 1u);
    }

    World::instance().destroyEntity(e);
    rig.bridge.syncEntityToPhysics();
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(convex_hull_and_offset_reach_collider_desc)
{
    // R10: ConvexHull spec -> ColliderDesc.shapeData (point cloud) and
    // spec.offset -> ColliderDesc.offset, verified on the captured payload.
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);
    ayt::physics::testaccess::reset();

    Entity* e = World::instance().createEntity();
    auto* t = e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    t->setPosition(0.0f, 0.0f, 0.0f);
    auto* collider = e->addComponent<ColliderComponent>();
    ColliderShapeSpec& spec = collider->addShape();
    spec.shape = ColliderShapeType::ConvexHull;
    spec.hullPoints = {
        ayt::math::FVector3(0.0f, 0.0f, 0.0f),
        ayt::math::FVector3(1.0f, 0.0f, 0.0f),
        ayt::math::FVector3(0.0f, 1.0f, 0.0f),
        ayt::math::FVector3(0.0f, 0.0f, 1.0f),
    };
    spec.offset = ayt::math::FVector3(0.0f, 0.25f, 0.0f);

    rig.bridge.syncEntityToPhysics();
    rig.step();
    const auto& payloads = ayt::physics::testaccess::mockBackendCreatePayloads();
    bool foundHull = false;
    for (const auto& p : payloads) {
        if (p.kind != ayt::physics::PhysicsCreatePayload::Kind::Collider) {
            continue;
        }
        const auto& cd = p.colliderDesc;
        if (cd.shape != ayt::physics::ColliderShape::ConvexHull) {
            continue;
        }
        CHECK_NOT_NULL(cd.shapeData.get());
        CHECK_INT_EQ(static_cast<int>(cd.shapeData->hullPoints.size()), 4);
        CHECK_FLOAT_EQ(cd.shapeData->hullPoints[3].z, 1.0f, 0.0001f);
        CHECK_FLOAT_EQ(cd.offset.x, 0.0f, 0.0001f);
        CHECK_FLOAT_EQ(cd.offset.y, 0.25f, 0.0001f);
        foundHull = true;
    }
    CHECK(foundHull);

    World::instance().destroyEntity(e);
    rig.bridge.syncEntityToPhysics();
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_CASE(offset_change_rebuilds_collider)
{
    // R10: offset participates in the shape-equality fallback — editing it
    // directly on the spec vector (bypassing revision) must rebuild.
    ayt::physics::PhysicsBackendDescriptor desc;
    desc.kind3D = ayt::physics::BackendKind::Mock;
    TestRig rig(desc);
    ayt::physics::testaccess::reset();

    Entity* e = World::instance().createEntity();
    auto* t = e->addComponent<Transform>();
    auto* rb = e->addComponent<RigidBodyComponent>();
    rb->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    t->setPosition(0.0f, 0.0f, 0.0f);
    auto* collider = e->addComponent<ColliderComponent>();
    collider->addShape();  // default Box

    rig.bridge.syncEntityToPhysics();
    rig.step();
    ayt::physics::testaccess::reset();

    // Direct vector write (revision NOT bumped): bridge must detect via the
    // deep compare and rebuild -> a second CreateCollider command.
    collider->shapes[0].offset = ayt::math::FVector3(1.0f, 0.0f, 0.0f);
    rig.bridge.syncEntityToPhysics();
    rig.step();
    {
        const auto& cmds = ayt::physics::testaccess::mockBackendCommands();
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::CreateCollider) == 1u);
        CHECK(countCommands(cmds, ayt::physics::PhysicsCommandType::DestroyCollider) == 1u);
    }

    World::instance().destroyEntity(e);
    rig.bridge.syncEntityToPhysics();
    rig.manager->shutdown();
    World::instance().shutdown();
}

TEST_SUITE_END
