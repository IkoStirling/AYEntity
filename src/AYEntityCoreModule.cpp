#include <AYEntity/EntityModule.h>

#include <AYEntity/SimToPresentBridgeSystem.h>
#include <AYEntity/World.h>

#include <cstring>

namespace ayt::entity
{
namespace
{

bool hasSystemNamed(const World& world, const char* name)
{
    for (size_t i = 0; i < world.systemCount(); ++i) {
        const char* existing = world.getSystemNameAt(i);
        if (existing != nullptr && std::strcmp(existing, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

void bootstrapEntityCore()
{
    registerEntitySubSystem();
    (void)registerEntityCoreComponents(ComponentRegistry::instance());
    registerEntityCoreSystems();
}

void registerEntityCoreSystems()
{
    World& world = World::instance();
    if (!hasSystemNamed(world, "SimToPresentBridgeSystem")) {
        registerSimToPresentBridgeSystem();
    }
}

} // namespace ayt::entity
