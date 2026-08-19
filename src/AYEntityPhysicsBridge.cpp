// AYEntityPhysicsBridge.cpp - Entity<->Physics bridge implementation.
//
// The bridge is driven by EntitySubSystem (FixedPrePhysics / FixedPostPhysics)
// and by tests directly (setPhysicsManager + syncEntityToPhysics).

#include "AYEntity/EntityPhysicsBridge.h"

#include <AYEntity.h>
#include <AYLog.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsSubSystem.h>
#include <AYPhysics/PhysicsWorld2D.h>
#include <AYPhysics/PhysicsWorld3D.h>

namespace ayt::entity
{

// =============================================================================
// Manager resolution
// =============================================================================

ayt::physics::PhysicsManager* EntityPhysicsBridge::resolveManager() const
{
    if (_managerOverride != nullptr) {
        return _managerOverride;
    }
    auto* physics = ayt::physics::PhysicsSubSystem::findRegistered();
    return physics != nullptr ? physics->manager() : nullptr;
}

void EntityPhysicsBridge::setPhysicsManager(ayt::physics::PhysicsManager* mgr)
{
    _managerOverride = mgr;
}

ayt::physics::PhysicsManager* EntityPhysicsBridge::physicsManager() const
{
    return _managerOverride != nullptr
        ? _managerOverride
        : resolveManager();
}

// =============================================================================
// Key / compare helpers
// =============================================================================

uint64_t EntityPhysicsBridge::bodyKey(
    ayt::physics::BodyHandle body,
    RigidBodyComponent::PhysicsDimension dimension) noexcept
{
    const uint64_t domain = dimension == RigidBodyComponent::PhysicsDimension::TwoD
        ? 1ull
        : 0ull;
    return (domain << 32u) | static_cast<uint64_t>(body);
}

uint64_t EntityPhysicsBridge::bodyKey(
    const ayt::physics::BodyTransform& body) noexcept
{
    const uint64_t domain = body.dimension == ayt::physics::PhysicsDimension::TwoD
        ? 1ull
        : 0ull;
    return (domain << 32u) | static_cast<uint64_t>(body.body);
}

bool EntityPhysicsBridge::poseEquals(const PhysicsBinding& binding,
                                     const Transform& transform) noexcept
{
    return binding.submittedPosition.x == transform.position.x
        && binding.submittedPosition.y == transform.position.y
        && binding.submittedPosition.z == transform.position.z
        && binding.submittedRotation.x == transform.rotation.x
        && binding.submittedRotation.y == transform.rotation.y
        && binding.submittedRotation.z == transform.rotation.z
        && binding.submittedRotation.w == transform.rotation.w;
}

bool EntityPhysicsBridge::shapesEqual(
    const std::vector<ColliderShapeSpec>& a,
    const std::vector<ColliderShapeSpec>& b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const ColliderShapeSpec& x = a[i];
        const ColliderShapeSpec& y = b[i];
        if (x.shape != y.shape
            || x.halfExtents.x != y.halfExtents.x
            || x.halfExtents.y != y.halfExtents.y
            || x.halfExtents.z != y.halfExtents.z
            || x.radius != y.radius
            || x.height != y.height
            || x.isTrigger != y.isTrigger
            || x.friction != y.friction
            || x.restitution != y.restitution
            || x.offset.x != y.offset.x
            || x.offset.y != y.offset.y
            || x.offset.z != y.offset.z
            || x.hullPoints != y.hullPoints) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// Descriptor mapping (component fields -> AYPhysics descriptors)
// =============================================================================

void EntityPhysicsBridge::buildRigidbodyDesc(ayt::physics::RigidbodyDesc& desc,
                                             const Transform& transform,
                                             const RigidBodyComponent& rigidBody) noexcept
{
    if (rigidBody.isStatic()) {
        desc.type = ayt::physics::BodyType::Static;
    } else if (rigidBody.isKinematic()) {
        desc.type = ayt::physics::BodyType::Kinematic;
    } else {
        desc.type = ayt::physics::BodyType::Dynamic;
    }
    desc.position = transform.position;
    desc.rotation = transform.rotation;
    desc.mass = rigidBody.getMass() > 0.0f ? rigidBody.getMass() : 1.0f;
    desc.linearVelocity = {rigidBody.getVelocityX(),
                           rigidBody.getVelocityY(),
                           rigidBody.getVelocityZ()};
    desc.material.friction = rigidBody.getFriction();
    desc.material.restitution = rigidBody.getRestitution();
    desc.gravityScale = rigidBody.getGravityScale();
}

bool EntityPhysicsBridge::buildColliderDesc(ayt::physics::ColliderDesc& desc,
                                            const ColliderShapeSpec& spec) noexcept
{
    switch (spec.shape) {
    case ColliderShapeType::Box:
        desc.shape = ayt::physics::ColliderShape::Box;
        desc.halfExtents = spec.halfExtents;
        break;
    case ColliderShapeType::Sphere:
        desc.shape = ayt::physics::ColliderShape::Sphere;
        desc.radius = spec.radius;
        break;
    case ColliderShapeType::Capsule:
        desc.shape = ayt::physics::ColliderShape::Capsule;
        desc.radius = spec.radius;
        desc.height = spec.height;
        break;
    case ColliderShapeType::ConvexHull: {
        if (spec.hullPoints.size() < 3u) {
            return false;  // degenerate — backend would reject anyway
        }
        desc.shape = ayt::physics::ColliderShape::ConvexHull;
        auto data = std::make_shared<ayt::physics::ColliderShapeData>();
        data->hullPoints = spec.hullPoints;
        desc.shapeData = std::move(data);
        break;
    }
    default:
        return false;
    }
    desc.material.friction = spec.friction;
    desc.material.restitution = spec.restitution;
    desc.isTrigger = spec.isTrigger;
    desc.offset = spec.offset;
    return true;
}

// =============================================================================
// World routing (2D / 3D)
// =============================================================================

ayt::physics::PhysResult EntityPhysicsBridge::createBodyOn(
    const ayt::physics::RigidbodyDesc& desc,
    RigidBodyComponent::PhysicsDimension dimension,
    ayt::physics::BodyHandle& outHandle) const
{
    if (_boundPhysicsManager == nullptr) {
        return ayt::physics::PhysResult::InvalidState;
    }
    if (dimension == RigidBodyComponent::PhysicsDimension::TwoD) {
        if (auto* world = _boundPhysicsManager->world2D()) {
            return world->createRigidbody(desc, outHandle);
        }
    } else if (auto* world = _boundPhysicsManager->world3D()) {
        return world->createRigidbody(desc, outHandle);
    }
    return ayt::physics::PhysResult::InvalidState;
}

ayt::physics::PhysResult EntityPhysicsBridge::createColliderOn(
    const ayt::physics::ColliderDesc& desc,
    RigidBodyComponent::PhysicsDimension dimension,
    ayt::physics::ColliderHandle& outHandle) const
{
    if (_boundPhysicsManager == nullptr) {
        return ayt::physics::PhysResult::InvalidState;
    }
    if (dimension == RigidBodyComponent::PhysicsDimension::TwoD) {
        if (auto* world = _boundPhysicsManager->world2D()) {
            return world->createCollider(desc, outHandle);
        }
    } else if (auto* world = _boundPhysicsManager->world3D()) {
        return world->createCollider(desc, outHandle);
    }
    return ayt::physics::PhysResult::InvalidState;
}

void EntityPhysicsBridge::destroyBody(
    ayt::physics::BodyHandle body,
    RigidBodyComponent::PhysicsDimension dimension) const
{
    if (_boundPhysicsManager == nullptr) {
        return;
    }
    if (dimension == RigidBodyComponent::PhysicsDimension::TwoD) {
        if (auto* world = _boundPhysicsManager->world2D()) {
            world->destroyRigidbody(body);
        }
    } else if (auto* world = _boundPhysicsManager->world3D()) {
        world->destroyRigidbody(body);
    }
}

void EntityPhysicsBridge::releaseColliders(PhysicsBinding& binding)
{
    if (binding.colliderHandles.empty()) {
        return;
    }
    if (_boundPhysicsManager != nullptr) {
        if (binding.dimension == RigidBodyComponent::PhysicsDimension::TwoD) {
            if (auto* world = _boundPhysicsManager->world2D()) {
                for (auto h : binding.colliderHandles) {
                    world->destroyCollider(h);
                }
            }
        } else if (auto* world = _boundPhysicsManager->world3D()) {
            for (auto h : binding.colliderHandles) {
                world->destroyCollider(h);
            }
        }
    }
    binding.colliderHandles.clear();
}

// =============================================================================
// syncEntityToPhysics — FixedPrePhysics
// =============================================================================

void EntityPhysicsBridge::syncEntityToPhysics()
{
    auto* manager = resolveManager();
    if (manager == nullptr) {
        // Manager gone: any body it owned is gone with it; just drop tables.
        _bindingsByEntity.clear();
        _entityByBody.clear();
        _boundPhysicsManager = nullptr;
        return;
    }
    if (manager != _boundPhysicsManager) {
        // Different manager: release bodies owned under the old one (it is
        // still alive — this is a handoff, not a teardown).
        destroyOwnedBodies();
        _bindingEpoch = 0;
        _boundPhysicsManager = manager;
    }

    ++_bindingEpoch;
    if (_bindingEpoch == 0) {
        _bindingsByEntity.clear();
        _entityByBody.clear();
        _bindingEpoch = 1;
    }

    if (auto* rigidBodies = World::instance().getStorage<RigidBodyComponent>()) {
        _bindingsByEntity.reserve(rigidBodies->size());
        _entityByBody.reserve(rigidBodies->size());
    }

    uint32_t rejectedCommands = 0;
    ayt::physics::PhysResult firstFailure = ayt::physics::PhysResult::Ok;
    auto query = World::instance().query<Transform, RigidBodyComponent>();
    for (Entity* entity : query) {
        auto* transform = entity->getComponent<Transform>();
        auto* rigidBody = entity->getComponent<RigidBodyComponent>();
        if (transform == nullptr || rigidBody == nullptr) {
            continue;
        }

        const uint32_t entityId = entity->getId();
        const ayt::physics::BodyHandle body = rigidBody->getBodyHandle();
        const auto dimension = rigidBody->getPhysicsDimension();
        const auto syncMode = rigidBody->getSyncMode();
        auto [bindingIt, inserted] =
            _bindingsByEntity.try_emplace(entityId);
        PhysicsBinding& binding = bindingIt->second;

        const bool identityChanged = inserted
            || binding.entity != entity
            || binding.body != body
            || binding.dimension != dimension
            || binding.syncMode != syncMode;
        if (identityChanged && !inserted) {
            removeBodyLookup(entityId, binding);
            // Stale colliders belong to the previous body: release them with
            // the OLD dimension before any fields are overwritten.
            releaseColliders(binding);
            if (binding.ownsBody && binding.body != ayt::physics::InvalidBodyHandle) {
                destroyBody(binding.body, binding.dimension);
            }
            binding.ownsBody = false;
        }

        binding.entity = entity;
        binding.transform = transform;
        binding.rigidBody = rigidBody;
        binding.body = body;
        binding.dimension = dimension;
        binding.syncMode = syncMode;
        binding.seenEpoch = _bindingEpoch;
        if (identityChanged) binding.hasSubmittedPose = false;

        // --- Body creation (parameter bridge: component fields -> desc).
        if (binding.body == ayt::physics::InvalidBodyHandle) {
            if (syncMode == RigidBodyComponent::SyncMode::None) {
                continue;
            }
            ayt::physics::RigidbodyDesc desc;
            buildRigidbodyDesc(desc, *transform, *rigidBody);
            ayt::physics::BodyHandle created = ayt::physics::InvalidBodyHandle;
            const ayt::physics::PhysResult result =
                createBodyOn(desc, dimension, created);
            if (result != ayt::physics::PhysResult::Ok) {
                // Keep the binding; creation retries next tick.
                if (rejectedCommands == 0) firstFailure = result;
                ++rejectedCommands;
                continue;
            }
            binding.body = created;
            binding.ownsBody = true;
            rigidBody->setBodyHandle(created);
            binding.hasSubmittedPose = false;
        }

        // --- Collider sync (create / rebuild / release).
        syncColliders(binding, entity, rejectedCommands, firstFailure);

        // --- R10 runtime-parameter sync + force/torque flush.
        syncRuntimeParams(binding, rejectedCommands, firstFailure);
        flushEntityForces(binding, rejectedCommands, firstFailure);

        if (syncMode == RigidBodyComponent::SyncMode::PhysicsToEntity) {
            _entityByBody[bodyKey(binding.body, dimension)] = entityId;
            continue;
        }
        // Fast path: setter writes bump Transform::revision, so unchanged
        // entities skip on an int compare. Direct field writes bypass the
        // counter, so poseEquals stays as the fallback for those.
        if (syncMode != RigidBodyComponent::SyncMode::EntityToPhysics
            || (binding.hasSubmittedPose
                && binding.lastSyncedRevision == transform->revision
                && poseEquals(binding, *transform))) {
            continue;
        }

        ayt::physics::PhysResult result = ayt::physics::PhysResult::InvalidState;
        if (dimension == RigidBodyComponent::PhysicsDimension::TwoD) {
            if (auto* world = manager->world2D()) {
                result = world->setRigidbodyTransform(
                    body, transform->position, transform->rotation);
            }
        } else if (auto* world = manager->world3D()) {
            result = world->setRigidbodyTransform(
                body, transform->position, transform->rotation);
        }

        if (result == ayt::physics::PhysResult::Ok) {
            binding.submittedPosition = transform->position;
            binding.submittedRotation = transform->rotation;
            binding.lastSyncedRevision = transform->revision;
            binding.hasSubmittedPose = true;
        } else {
            if (rejectedCommands == 0) firstFailure = result;
            ++rejectedCommands;
        }
    }

    // Sweep bindings whose entity vanished from the query (destroyed or
    // component removed): release colliders + destroy owned bodies.
    for (auto it = _bindingsByEntity.begin(); it != _bindingsByEntity.end();) {
        PhysicsBinding& stale = it->second;
        if (stale.seenEpoch == _bindingEpoch) {
            ++it;
            continue;
        }
        removeBodyLookup(it->first, stale);
        releaseColliders(stale);
        if (stale.ownsBody && stale.body != ayt::physics::InvalidBodyHandle) {
            destroyBody(stale.body, stale.dimension);
        }
        it = _bindingsByEntity.erase(it);
    }

    if (rejectedCommands != 0) {
        ayt::log::warn(
            "[Entity] Physics commands rejected: count=%u first=%s",
            rejectedCommands,
            ayt::physics::toString(firstFailure));
    }
}

// =============================================================================
// syncColliders — create / rebuild / release colliders for a binding
// =============================================================================

void EntityPhysicsBridge::syncColliders(PhysicsBinding& binding,
                                        Entity* entity,
                                        uint32_t& rejectedCommands,
                                        ayt::physics::PhysResult& firstFailure)
{
    auto* collider = entity->getComponent<ColliderComponent>();
    const uint64_t revision = collider != nullptr ? collider->revision : 0u;

    const bool changed = binding.collider != collider
        || binding.lastSyncedColliderRevision != revision
        || (collider != nullptr
                ? !shapesEqual(binding.lastSyncedShapes, collider->shapes)
                : !binding.lastSyncedShapes.empty());
    if (!changed) {
        return;
    }

    releaseColliders(binding);
    binding.colliderHandles.clear();
    binding.lastSyncedShapes.clear();
    binding.lastSyncedColliderRevision = 0;
    binding.collider = collider;
    if (collider == nullptr) {
        return;
    }

    for (const ColliderShapeSpec& spec : collider->shapes) {
        ayt::physics::ColliderDesc desc;
        desc.body = binding.body;
        if (!buildColliderDesc(desc, spec)) {
            ayt::log::warn(
                "[Entity] Collider shape kind %u skipped (unknown)",
                static_cast<unsigned>(spec.shape));
            continue;
        }
        ayt::physics::ColliderHandle handle = ayt::physics::InvalidColliderHandle;
        const ayt::physics::PhysResult result =
            createColliderOn(desc, binding.dimension, handle);
        if (result == ayt::physics::PhysResult::Ok) {
            binding.colliderHandles.push_back(handle);
        } else {
            if (rejectedCommands == 0) firstFailure = result;
            ++rejectedCommands;
        }
    }
    binding.lastSyncedShapes = collider->shapes;
    binding.lastSyncedColliderRevision = collider->revision;
}

// =============================================================================
// =============================================================================
// R10 — runtime parameter sync (incremental commands) + force/torque flush
// =============================================================================

void EntityPhysicsBridge::syncRuntimeParams(
    PhysicsBinding& binding,
    uint32_t& rejectedCommands,
    ayt::physics::PhysResult& firstFailure)
{
    auto* manager = _boundPhysicsManager;
    RigidBodyComponent* rigidBody = binding.rigidBody;
    if (manager == nullptr || rigidBody == nullptr) return;
    const ayt::physics::BodyHandle body = binding.body;
    if (body == ayt::physics::InvalidBodyHandle) return;
    const bool is2D =
        binding.dimension == RigidBodyComponent::PhysicsDimension::TwoD;

    // Fresh binding: the create descriptor already carried mass / friction /
    // restitution / gravityScale / velocity — baseline from the component
    // without re-sending (a diff against zeros would re-push every field).
    if (!binding.paramsSynced) {
        binding.lastSyncedMass = rigidBody->getMass();
        binding.lastSyncedFriction = rigidBody->getFriction();
        binding.lastSyncedRestitution = rigidBody->getRestitution();
        binding.lastSyncedGravityScale = rigidBody->getGravityScale();
        binding.lastSyncedVelocity[0] = rigidBody->getVelocityX();
        binding.lastSyncedVelocity[1] = rigidBody->getVelocityY();
        binding.lastSyncedVelocity[2] = rigidBody->getVelocityZ();
        binding.paramsSynced = true;
        return;
    }

    auto emit = [&](ayt::physics::PhysResult result) {
        if (result == ayt::physics::PhysResult::Ok) return true;
        if (rejectedCommands == 0) firstFailure = result;
        ++rejectedCommands;
        return false;
    };

    // --- mass
    if (rigidBody->getMass() != binding.lastSyncedMass) {
        ayt::physics::PhysResult result = ayt::physics::PhysResult::InvalidState;
        if (is2D) {
            if (auto* w = manager->world2D()) {
                result = w->setMass(body, rigidBody->getMass());
            }
        } else if (auto* w = manager->world3D()) {
            result = w->setMass(body, rigidBody->getMass());
        }
        if (emit(result)) binding.lastSyncedMass = rigidBody->getMass();
    }

    // --- friction / restitution (collider-scoped: Box2D per-shape, Jolt
    // resolves the collider to its owning body). Apply to every bound
    // collider; with no colliders yet, keep the diff for next tick.
    const float friction = rigidBody->getFriction();
    const float restitution = rigidBody->getRestitution();
    if (friction != binding.lastSyncedFriction
        || restitution != binding.lastSyncedRestitution) {
        for (const ayt::physics::ColliderHandle c : binding.colliderHandles) {
            ayt::physics::PhysResult result = ayt::physics::PhysResult::InvalidState;
            if (is2D) {
                if (auto* w = manager->world2D()) {
                    result = w->setMaterial(c, friction, restitution);
                }
            } else if (auto* w = manager->world3D()) {
                result = w->setMaterial(c, friction, restitution);
            }
            if (!emit(result)) return;  // queue full — retry next tick
        }
        binding.lastSyncedFriction = friction;
        binding.lastSyncedRestitution = restitution;
    }

    // --- gravity scale
    const float gs = rigidBody->getGravityScale();
    if (gs != binding.lastSyncedGravityScale) {
        ayt::physics::PhysResult result = ayt::physics::PhysResult::InvalidState;
        if (is2D) {
            if (auto* w = manager->world2D()) {
                result = w->setGravityScale(body, gs);
            }
        } else if (auto* w = manager->world3D()) {
            result = w->setGravityScale(body, gs);
        }
        if (emit(result)) binding.lastSyncedGravityScale = gs;
    }

    // --- linear velocity (one-shot: a later same-value set does not re-push,
    // so the physics simulation is not pinned to the component value).
    const float vx = rigidBody->getVelocityX();
    const float vy = rigidBody->getVelocityY();
    const float vz = rigidBody->getVelocityZ();
    if (vx != binding.lastSyncedVelocity[0]
        || vy != binding.lastSyncedVelocity[1]
        || vz != binding.lastSyncedVelocity[2]) {
        ayt::physics::PhysResult result = ayt::physics::PhysResult::InvalidState;
        if (is2D) {
            if (auto* w = manager->world2D()) {
                result = w->setRigidbodyVelocity(
                    body, ayt::math::FVector3(vx, vy, vz));
            }
        } else if (auto* w = manager->world3D()) {
            result = w->setRigidbodyVelocity(
                body, ayt::math::FVector3(vx, vy, vz));
        }
        if (emit(result)) {
            binding.lastSyncedVelocity[0] = vx;
            binding.lastSyncedVelocity[1] = vy;
            binding.lastSyncedVelocity[2] = vz;
        }
    }
}

void EntityPhysicsBridge::flushEntityForces(
    PhysicsBinding& binding,
    uint32_t& rejectedCommands,
    ayt::physics::PhysResult& firstFailure)
{
    auto* manager = _boundPhysicsManager;
    RigidBodyComponent* rigidBody = binding.rigidBody;
    if (manager == nullptr || rigidBody == nullptr) return;
    const ayt::physics::BodyHandle body = binding.body;
    if (body == ayt::physics::InvalidBodyHandle) return;
    const bool is2D =
        binding.dimension == RigidBodyComponent::PhysicsDimension::TwoD;

    auto emit = [&](ayt::physics::PhysResult result) {
        if (result == ayt::physics::PhysResult::Ok) return;
        if (rejectedCommands == 0) firstFailure = result;
        ++rejectedCommands;
    };

    // Accumulated force — pushed every tick while non-zero, cleared only on
    // a successful push so a QueueFull keeps the intent for the next tick.
    const float fx = rigidBody->getForceX();
    const float fy = rigidBody->getForceY();
    const float fz = rigidBody->getForceZ();
    if (fx != 0.0f || fy != 0.0f || fz != 0.0f) {
        ayt::physics::PhysResult result = ayt::physics::PhysResult::InvalidState;
        if (is2D) {
            if (auto* w = manager->world2D()) {
                result = w->applyForce(body, ayt::math::FVector3(fx, fy, fz));
            }
        } else if (auto* w = manager->world3D()) {
            result = w->applyForce(body, ayt::math::FVector3(fx, fy, fz));
        }
        emit(result);
        if (result == ayt::physics::PhysResult::Ok) rigidBody->clearForces();
    }

    // Accumulated torque. 2D worlds read only the Z component.
    const float tx = rigidBody->getTorqueX();
    const float ty = rigidBody->getTorqueY();
    const float tz = rigidBody->getTorqueZ();
    if (tx != 0.0f || ty != 0.0f || tz != 0.0f) {
        ayt::physics::PhysResult result = ayt::physics::PhysResult::InvalidState;
        if (is2D) {
            if (auto* w = manager->world2D()) {
                result = w->applyTorque(body, tz);
            }
        } else if (auto* w = manager->world3D()) {
            result = w->applyTorque(body, ayt::math::FVector3(tx, ty, tz));
        }
        emit(result);
        if (result == ayt::physics::PhysResult::Ok) rigidBody->clearTorques();
    }
}

// syncPhysicsToEntity — FixedPostPhysics
// =============================================================================

void EntityPhysicsBridge::syncPhysicsToEntity()
{
    auto* manager = resolveManager();
    if (manager == nullptr) {
        return;
    }

    const ayt::physics::PhysFrameSnapshot& snapshot = manager->fetchResults();
    for (const ayt::physics::BodyTransform& body : snapshot.transforms) {
        auto entityIt = _entityByBody.find(bodyKey(body));
        if (entityIt == _entityByBody.end()) {
            continue;
        }
        auto bindingIt = _bindingsByEntity.find(entityIt->second);
        if (bindingIt == _bindingsByEntity.end()) {
            continue;
        }

        PhysicsBinding& binding = bindingIt->second;
        if (binding.entity == nullptr || !binding.entity->isValid()
            || binding.transform == nullptr || binding.rigidBody == nullptr
            || binding.syncMode != RigidBodyComponent::SyncMode::PhysicsToEntity) {
            continue;
        }
        binding.transform->applySimulationPose(body.position, body.rotation);
        binding.rigidBody->setVelocity(body.linearVelocity.x,
                                       body.linearVelocity.y,
                                       body.linearVelocity.z);
    }
}

// =============================================================================
// Teardown
// =============================================================================

void EntityPhysicsBridge::destroyOwnedBodies()
{
    for (auto& entry : _bindingsByEntity) {
        PhysicsBinding& binding = entry.second;
        releaseColliders(binding);
        if (binding.ownsBody && binding.body != ayt::physics::InvalidBodyHandle) {
            destroyBody(binding.body, binding.dimension);
        }
    }
    _bindingsByEntity.clear();
    _entityByBody.clear();
}

void EntityPhysicsBridge::shutdown()
{
    // Deliberately does NOT destroy bodies: the PhysicsManager's own shutdown
    // tears down the backend world, which frees every body it owns. Destroying
    // here would risk a dangling manager if PhysicsSubSystem shut down first.
    _bindingsByEntity.clear();
    _entityByBody.clear();
    _boundPhysicsManager = nullptr;
    _managerOverride = nullptr;
}

void EntityPhysicsBridge::removeBodyLookup(uint32_t entityId,
                                           const PhysicsBinding& binding)
{
    if (binding.body == ayt::physics::InvalidBodyHandle
        || binding.syncMode != RigidBodyComponent::SyncMode::PhysicsToEntity) {
        return;
    }
    const uint64_t key = bodyKey(binding.body, binding.dimension);
    auto it = _entityByBody.find(key);
    if (it != _entityByBody.end() && it->second == entityId) {
        _entityByBody.erase(it);
    }
}

} // namespace ayt::entity
