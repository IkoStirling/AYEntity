// AYEntityReflection.cpp - component type registration for World

#include "AYEntityModule.h"
#include "AYWorld.h"
#include "components/AYTransformComponent.h"
#include "components/AYHealthComponent.h"

namespace ayt::entity
{

void registerEntityComponents()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    // Parentheses prevent Win32 GDI macro "Transform" from breaking the template arg.
    World::registerComponentType<Transform>("Transform");
    World::registerComponentType<HealthComponent>("HealthComponent");
}

} // namespace ayt::entity
