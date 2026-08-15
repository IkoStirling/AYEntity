// AYEntitySubSystem.cpp - AYEntity 子系统实现

#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
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
            .timeType = ayt::game::SubSystemDescriptor::TimeType::Scaled
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
    }

private:
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

