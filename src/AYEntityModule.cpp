#include <AYEntity/EntityModule.h>

#if AY_ENTITY_HAS_ANIMATION_INTEGRATION
#include <AYEntity/EntityAnimationIntegrationModule.h>
#endif
#if AY_ENTITY_HAS_RENDER_INTEGRATION
#include <AYEntity/EntityRenderIntegrationModule.h>
#endif
#if AY_ENTITY_HAS_2D_COMPONENTS
#include <AYEntity/Entity2DIntegrationModule.h>
#endif
#if AY_ENTITY_HAS_PHYSICS_INTEGRATION
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#endif
#if AY_ENTITY_HAS_SCRIPT_INTEGRATION
#include <AYEntity/EntityScriptIntegrationModule.h>
#endif
#if AY_ENTITY_HAS_NETWORK_INTEGRATION
#include <AYEntity/EntityNetworkIntegrationModule.h>
#endif

#include <cstdio>

namespace ayt::entity
{

ComponentRegistryResult registerEntityComponents(ComponentRegistry& registry)
{
    ComponentRegistryResult result = registerEntityCoreComponents(registry);
    if (!result) return result;

#if AY_ENTITY_HAS_ANIMATION_INTEGRATION
    result = registerEntityAnimationComponents(registry);
    if (!result) return result;
#endif
#if AY_ENTITY_HAS_RENDER_INTEGRATION
    result = registerEntityRenderComponents(registry);
    if (!result) return result;
#endif
#if AY_ENTITY_HAS_2D_COMPONENTS
    result = registerEntity2DComponents(registry);
    if (!result) return result;
#endif
#if AY_ENTITY_HAS_PHYSICS_INTEGRATION
    result = registerEntityPhysicsComponents(registry);
    if (!result) return result;
#endif
#if AY_ENTITY_HAS_SCRIPT_INTEGRATION
    result = registerEntityScriptComponents(registry);
    if (!result) return result;
#endif
#if AY_ENTITY_HAS_NETWORK_INTEGRATION
    result = registerEntityNetworkComponents(registry);
    if (!result) return result;
#endif
    return ComponentRegistryResult::success();
}

void registerEntityComponents()
{
    (void)registerEntityComponents(ComponentRegistry::instance());
}

void registerEntityPresentationSystems()
{
#if AY_ENTITY_HAS_ANIMATION_INTEGRATION
    registerEntityAnimationSystems();
#endif
#if AY_ENTITY_HAS_RENDER_INTEGRATION
    registerEntityRenderSystems();
#endif
#if AY_ENTITY_HAS_2D_INTEGRATION
    registerEntity2DSystems();
#endif
}

void bootstrapModule()
{
    registerEntitySubSystem();
    registerEntityComponents();
    registerEntityCoreSystems();
    registerEntityPresentationSystems();

    static bool loggedOnce = false;
    if (!loggedOnce) {
        std::fprintf(
            stderr,
            "[AYEntity] compatibility facade bootstrap complete\n");
        loggedOnce = true;
    }
}

} // namespace ayt::entity
