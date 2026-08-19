// AYEntitySubSystem.cpp - AYEntity 子系统实现
//
// Thin GameLoop adapter over EntityPhysicsBridge (the binding tables + sync
// logic live in AYEntityPhysicsBridge.cpp). Also owns the World
// process/Scene redirect (World::instance()).

#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
#include "AYEntity/EntityPhysicsBridge.h"
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYGameLoop.h>
#include <cstdio>

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
        _bridge.shutdown();
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
        _bridge.syncPhysicsToEntity();
    }

    void tick(ayt::game::FramePhase phase,
              const ayt::game::FrameContext& context) override {
        if (phase == ayt::game::FramePhase::FixedPrePhysics) {
            _bridge.syncEntityToPhysics();
        } else if (phase == ayt::game::FramePhase::FixedPostPhysics) {
            _bridge.syncPhysicsToEntity();
        } else if (phase == ayt::game::FramePhase::World) {
            update(context.deltaTime);
        }
    }

private:
    EntityPhysicsBridge _bridge;
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
