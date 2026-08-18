// AYEntitySubSystem.cpp - AYEntity 子系统实现

#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYGameLoop.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsSubSystem.h>
#include <AYPhysics/PhysicsWorld2D.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include <AYLog.h>
#include <cstdio>
#include <cstdint>
#include <unordered_map>

namespace ayt::entity
{

// =============================================================================
// EntitySubSystem - 子系统实现
// =============================================================================
class EntitySubSystem : public ayt::game::ISubSystem {
public:
    const char* getName() const override { return "Entity"; }

    const ayt::game::SubSystemDescriptor& getDescriptor() const override {
        static ayt::game::SubSystemDescriptor desc = {
            .name = "Entity",
            .dependencies = {},
            .basePriority = 0,
            .timeType = ayt::game::SubSystemDescriptor::TimeType::Scaled,
            .phases = ayt::game::phaseBit(ayt::game::FramePhase::FixedPrePhysics)
                    | ayt::game::phaseBit(ayt::game::FramePhase::FixedPostPhysics)
                    | ayt::game::phaseBit(ayt::game::FramePhase::World),
            .clock = ayt::game::ClockDomain::Game,
            .phasePriority = 0,
            .reads = {"Simulation.World", "Physics.Snapshot"},
            .writes = {"Simulation.World", "Physics.Commands"}
        };
        return desc;
    }

    bool initialize() override {
        // Own only the process fallback. Scene Worlds are initialized by
        // Scene::Impl; do not clear an already-active Scene redirect here
        // (Application / SceneManager may setCurrent before GameLoop init).
        World::processWorld().initialize();
        ::printf("[Entity] Initialized\n");
        return true;
    }

    void shutdown() override {
        _bindingsByEntity.clear();
        _entityByBody.clear();
        _boundPhysicsManager = nullptr;
        // Drop Scene redirect, then shut down the process fallback only.
        // Scene RAII owns Scene World teardown.
        World::setActiveWorld(nullptr);
        World::processWorld().shutdown();
        ::printf("[Entity] Shutdown\n");
    }

    void update(float dt) override {
        // Redirects to Scene world when SceneManager::setCurrent is active.
        World::instance().update(dt);
    }

    void fixedUpdate(float fixedDeltaTime) override {
        (void)fixedDeltaTime;
        syncPhysicsToEntity();
    }

    void tick(ayt::game::FramePhase phase,
              const ayt::game::FrameContext& context) override {
        if (phase == ayt::game::FramePhase::FixedPrePhysics) {
            syncEntityToPhysics();
        } else if (phase == ayt::game::FramePhase::FixedPostPhysics) {
            syncPhysicsToEntity();
        } else if (phase == ayt::game::FramePhase::World) {
            update(context.deltaTime);
        }
    }

private:
    struct PhysicsBinding {
        Entity* entity = nullptr;
        Transform* transform = nullptr;
        RigidBodyComponent* rigidBody = nullptr;
        ayt::physics::BodyHandle body = ayt::physics::InvalidBodyHandle;
        RigidBodyComponent::PhysicsDimension dimension =
            RigidBodyComponent::PhysicsDimension::ThreeD;
        RigidBodyComponent::SyncMode syncMode = RigidBodyComponent::SyncMode::None;
        math::FVector3 submittedPosition{};
        math::FQuaternion submittedRotation{};
        uint64_t seenEpoch = 0;
        bool hasSubmittedPose = false;
    };

    static uint64_t bodyKey(
        ayt::physics::BodyHandle body,
        RigidBodyComponent::PhysicsDimension dimension) noexcept {
        const uint64_t domain = dimension == RigidBodyComponent::PhysicsDimension::TwoD
            ? 1ull
            : 0ull;
        return (domain << 32u) | static_cast<uint64_t>(body);
    }

    static uint64_t bodyKey(const ayt::physics::BodyTransform& body) noexcept {
        const uint64_t domain = body.dimension == ayt::physics::PhysicsDimension::TwoD
            ? 1ull
            : 0ull;
        return (domain << 32u) | static_cast<uint64_t>(body.body);
    }

    static bool poseEquals(const PhysicsBinding& binding,
                           const Transform& transform) noexcept {
        return binding.submittedPosition.x == transform.position.x
            && binding.submittedPosition.y == transform.position.y
            && binding.submittedPosition.z == transform.position.z
            && binding.submittedRotation.x == transform.rotation.x
            && binding.submittedRotation.y == transform.rotation.y
            && binding.submittedRotation.z == transform.rotation.z
            && binding.submittedRotation.w == transform.rotation.w;
    }

    void removeBodyLookup(uint32_t entityId, const PhysicsBinding& binding) {
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

    void syncEntityToPhysics() {
        auto* physics = ayt::physics::PhysicsSubSystem::findRegistered();
        auto* manager = physics != nullptr ? physics->manager() : nullptr;
        if (manager == nullptr) {
            _bindingsByEntity.clear();
            _entityByBody.clear();
            _boundPhysicsManager = nullptr;
            return;
        }
        if (manager != _boundPhysicsManager) {
            _bindingsByEntity.clear();
            _entityByBody.clear();
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
            }

            binding.entity = entity;
            binding.transform = transform;
            binding.rigidBody = rigidBody;
            binding.body = body;
            binding.dimension = dimension;
            binding.syncMode = syncMode;
            binding.seenEpoch = _bindingEpoch;
            if (identityChanged) binding.hasSubmittedPose = false;

            if (body == ayt::physics::InvalidBodyHandle) {
                continue;
            }
            if (syncMode == RigidBodyComponent::SyncMode::PhysicsToEntity) {
                _entityByBody[bodyKey(body, dimension)] = entityId;
                continue;
            }
            if (syncMode != RigidBodyComponent::SyncMode::EntityToPhysics
                || (binding.hasSubmittedPose && poseEquals(binding, *transform))) {
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
                binding.hasSubmittedPose = true;
            } else {
                if (rejectedCommands == 0) firstFailure = result;
                ++rejectedCommands;
            }
        }

        for (auto it = _bindingsByEntity.begin(); it != _bindingsByEntity.end();) {
            if (it->second.seenEpoch == _bindingEpoch) {
                ++it;
                continue;
            }
            removeBodyLookup(it->first, it->second);
            it = _bindingsByEntity.erase(it);
        }

        if (rejectedCommands != 0) {
            ayt::log::warn(
                "[Entity] Physics transform submission rejected: count=%u first=%s",
                rejectedCommands,
                ayt::physics::toString(firstFailure));
        }
    }

    void syncPhysicsToEntity() {
        auto* physics = ayt::physics::PhysicsSubSystem::findRegistered();
        auto* manager = physics != nullptr ? physics->manager() : nullptr;
        if (manager == nullptr) return;

        const ayt::physics::PhysFrameSnapshot& snapshot = manager->fetchResults();
        for (const ayt::physics::BodyTransform& body : snapshot.transforms) {
            auto entityIt = _entityByBody.find(bodyKey(body));
            if (entityIt == _entityByBody.end()) continue;
            auto bindingIt = _bindingsByEntity.find(entityIt->second);
            if (bindingIt == _bindingsByEntity.end()) continue;

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

    std::unordered_map<uint32_t, PhysicsBinding> _bindingsByEntity;
    std::unordered_map<uint64_t, uint32_t> _entityByBody;
    ayt::physics::PhysicsManager* _boundPhysicsManager = nullptr;
    uint64_t _bindingEpoch = 0;
};

// =============================================================================
// Registration (called from bootstrapModule)
// =============================================================================

void registerEntitySubSystem()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    ::ayt::game::IGameLoop::instance().registerSubSystem(new EntitySubSystem());
}

} // namespace ayt::entity
