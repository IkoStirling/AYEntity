// AYEntitySubSystem.cpp - AYEntity 子系统实现

#include "AYEntity.h"
#include "AYEntityModule.h"
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
        World::instance().initialize();
        ::printf("[Entity] Initialized\n");
        return true;
    }

    void shutdown() override {
        World::instance().shutdown();
        ::printf("[Entity] Shutdown\n");
    }

    void update(float dt) override {
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

