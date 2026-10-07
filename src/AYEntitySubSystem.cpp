// AYEntitySubSystem.cpp - AYEntity 子系统实现
//
// Thin GameLoop adapter that owns the World process/Scene redirect. Physics
// synchronization lives in AYEntityPhysicsIntegration so the ECS core has no
// dependency on a concrete physics backend.

#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
#include <AYEntity/EntitySimulationDriver.h>
#include <AYGameLoop.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace ayt::entity
{

// =============================================================================
// EntitySubSystem - 子系统实现
// =============================================================================
class EntitySubSystem : public ayt::game::ISubSystem {
public:
    ~EntitySubSystem() override { if (_simulationDriver) _simulationDriver->hostShutdown(); }
    const char* getName() const override { return "Entity"; }
    bool requiresOwnerThread() const noexcept override { return static_cast<bool>(_simulationDriver); }

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
        if (_simulationDriver) _simulationDriver->hostShutdown();
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
        if (!tickChecked(phase, context))
            throw std::runtime_error("Entity simulation policy rejected phase");
    }

    bool tickChecked(ayt::game::FramePhase phase,
                     const ayt::game::FrameContext& context) override {
        if (phase == ayt::game::FramePhase::FixedPrePhysics) {
            if (_simulationDriver) {
                auto handled = _simulationDriver->fixedTick(World::instance(), context);
                if (handled.has_value()) return *handled;
            }
            if (context.fixedStep) {
                World::instance().fixedUpdate(context.fixedStep->deltaTime());
            } else {
                World::instance().fixedUpdate(context.fixedDeltaTime);
            }
        } else if (phase == ayt::game::FramePhase::World) {
            if (_simulationDriver && !_simulationDriver->presentationBoundary(World::instance()))
                return false;
            World::instance().updatePresentation(
                context.deltaTime,
                _simulationDriver ? _simulationDriver->presentationAlpha(context.interpolationAlpha)
                    : context.interpolationAlpha);
        }
        return true;
    }

    TickResult tickResult(game::FramePhase phase, const game::FrameContext& context) override {
        if (tickChecked(phase, context)) return TickResult::Completed;
        return _simulationDriver && game::isFixedPhase(phase) ? TickResult::Blocked : TickResult::Failed;
    }

    bool attach(std::shared_ptr<IEntitySimulationDriver> driver) {
        if (!driver || _simulationDriver) return false;
        _simulationDriver = std::move(driver); return true;
    }
    bool detach(const IEntitySimulationDriver* driver) noexcept {
        if (!driver || _simulationDriver.get() != driver) return false;
        _simulationDriver->hostShutdown(); _simulationDriver.reset(); return true;
    }
private:
    std::shared_ptr<IEntitySimulationDriver> _simulationDriver;
};

bool attachEntitySimulationDriver(game::ISubSystem& system, std::shared_ptr<IEntitySimulationDriver> driver) {
    auto* entity = dynamic_cast<EntitySubSystem*>(&system);
    return entity && entity->attach(std::move(driver));
}
bool detachEntitySimulationDriver(game::ISubSystem& system, const IEntitySimulationDriver* driver) noexcept {
    auto* entity = dynamic_cast<EntitySubSystem*>(&system);
    return entity && entity->detach(driver);
}

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
