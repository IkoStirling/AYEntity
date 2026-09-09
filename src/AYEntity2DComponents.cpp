#include <AYEntity/Entity2DIntegrationModule.h>

#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/components/OrthoCameraComponent.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TilemapComponent.h>

namespace ayt::entity
{

AY_FINALIZE_REGISTRATION_METADATA(TilemapComponent)
AY_FINALIZE_REGISTRATION_METADATA(SpriteComponent)
AY_FINALIZE_REGISTRATION_METADATA(OrthoCameraComponent)

ComponentRegistryResult registerEntity2DComponents(ComponentRegistry& registry)
{
    if (auto result = registerSceneComponent<TilemapComponent>(
            registry, "TilemapComponent", "Tilemap", "2D");
        !result) {
        return result;
    }
    if (auto result = registerSceneComponent<SpriteComponent>(
            registry, "SpriteComponent", "Sprite", "2D");
        !result) {
        return result;
    }
    return registerSceneComponent<OrthoCameraComponent>(
        registry,
        "OrthoCameraComponent",
        "Orthographic Camera",
        "2D");
}

} // namespace ayt::entity
