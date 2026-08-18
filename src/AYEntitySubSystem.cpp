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
#include <cstdio>
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
    void syncEntityToPhysics() {
        auto* physics = ayt::physics::PhysicsSubSystem::findRegistered();
        auto* manager = physics != nullptr ? physics->manager() : nullptr;
        if (manager == nullptr) return;

        auto query = World::instance().query<Transform, RigidBodyComponent>();
        for (Entity* entity : query) {
            auto* transform = entity->getComponent<Transform>();
            auto* rigidBody = entity->getComponent<RigidBodyComponent>();
            if (transform == nullptr || rigidBody == nullptr
                || rigidBody->getBodyHandle() == ayt::physics::InvalidBodyHandle
                || rigidBody->getSyncMode() != RigidBodyComponent::SyncMode::EntityToPhysics) {
                continue;
            }

            if (rigidBody->getPhysicsDimension()
                == RigidBodyComponent::PhysicsDimension::TwoD) {
                if (auto* world = manager->world2D()) {
                    (void)world->setRigidbodyTransform(rigidBody->getBodyHandle(),
                                                       transform->position,
                                                       transform->rotation);
                }
            } else if (auto* world = manager->world3D()) {
                (void)world->setRigidbodyTransform(rigidBody->getBodyHandle(),
                                                   transform->position,
                                                   transform->rotation);
            }
        }
    }

    void syncPhysicsToEntity() {
        auto* physics = ayt::physics::PhysicsSubSystem::findRegistered();
        auto* manager = physics != nullptr ? physics->manager() : nullptr;
        if (manager == nullptr) return;

        std::unordered_map<ayt::physics::BodyHandle, Entity*> bindings;
        auto query = World::instance().query<Transform, RigidBodyComponent>();
        for (Entity* entity : query) {
            auto* rigidBody = entity->getComponent<RigidBodyComponent>();
            if (rigidBody != nullptr
                && rigidBody->getBodyHandle() != ayt::physics::InvalidBodyHandle
                && rigidBody->getSyncMode() == RigidBodyComponent::SyncMode::PhysicsToEntity) {
                bindings.emplace(rigidBody->getBodyHandle(), entity);
            }
        }

        const ayt::physics::PhysFrameSnapshot& snapshot = manager->fetchResults();
        for (const ayt::physics::BodyTransform& body : snapshot.transforms) {
            auto it = bindings.find(body.body);
            if (it == bindings.end()) continue;

            Entity* entity = it->second;
            if (auto* transform = entity->getComponent<Transform>()) {
                transform->applySimulationPose(body.position, body.rotation);
            }
            if (auto* rigidBody = entity->getComponent<RigidBodyComponent>()) {
                rigidBody->setVelocity(body.linearVelocity.x,
                                       body.linearVelocity.y,
                                       body.linearVelocity.z);
            }
        }
    }
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
