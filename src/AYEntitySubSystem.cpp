// AYEntitySubSystem.cpp - AYEntity 子系统实现
//
// Thin GameLoop adapter that owns the World process/Scene redirect. Physics
// synchronization lives in AYEntityPhysicsIntegration so the ECS core has no
// dependency on a concrete physics backend.

#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
#include <AYGameLoop.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <cstdio>
#include <memory>

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
                    | ayt::game::phaseBit(ayt::game::FramePhase::World),
            .clock = ayt::game::ClockDomain::Game,
            .phasePriority = 0,
            .reads = {"Simulation.World"},
            .writes = {"Simulation.World"}
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
        World::instance().fixedUpdate(fixedDeltaTime);
    }

    void tick(ayt::game::FramePhase phase,
              const ayt::game::FrameContext& context) override {
        if (phase == ayt::game::FramePhase::FixedPrePhysics) {
            if (context.fixedStep) {
                World::instance().fixedUpdate(context.fixedStep->deltaTime());
            } else {
                World::instance().fixedUpdate(context.fixedDeltaTime);
            }
        } else if (phase == ayt::game::FramePhase::World) {
            World::instance().updatePresentation(
                context.deltaTime,
                context.interpolationAlpha);
        }
    }
};

// =============================================================================
// Registration (called from bootstrapModule)
// =============================================================================

std::unique_ptr<ayt::game::ISubSystem> createEntitySubSystem()
{
    return std::make_unique<EntitySubSystem>();
}

void registerEntitySubSystem()
{
    if (ayt::game::SubSystemRegistry::instance().findSubSystem("Entity")
        != nullptr) {
        return;
    }
    auto system = createEntitySubSystem();
    ::ayt::game::IGameLoop::instance().registerSubSystem(system.release());
}

} // namespace ayt::entity
