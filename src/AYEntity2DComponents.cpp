#include <AYEntity/Entity2DIntegrationModule.h>

#include <AYEntity.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/components/OrthoCameraComponent.h>
#include <AYEntity/components/SpriteAnimationComponent.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TilemapComponent.h>
#include <AYEntity/components/TransformComponent.h>

namespace ayt::entity
{

AY_FINALIZE_REGISTRATION_METADATA(TilemapComponent)
AY_FINALIZE_REGISTRATION_METADATA(SpriteComponent)
AY_FINALIZE_REGISTRATION_METADATA(SpriteAnimationComponent)
AY_FINALIZE_REGISTRATION_METADATA(OrthoCameraComponent)

namespace {

Transform* addTransformIfMissing(Entity& entity)
{
    if (Transform* transform = entity.getComponent<Transform>()) {
        return transform;
    }
    return entity.addComponent<Transform>();
}

void migrateLegacyTilemapTransform(Entity& entity, IComponent&)
{
    (void)addTransformIfMissing(entity);
}

void migrateLegacySpriteTransform(Entity& entity, IComponent& component)
{
    if (entity.hasComponent<Transform>()) return;
    auto& sprite = static_cast<SpriteComponent&>(component);
    if (Transform* transform = addTransformIfMissing(entity)) {
        transform->position = sprite.position;
        transform->rotation = math::FQuaternion::fromAxisAngle(
            math::FVector3(0.0f, 0.0f, 1.0f), sprite.rotationZ);
        transform->scale = {sprite.scaleX, sprite.scaleY, 1.0f};
    }
}

void migrateLegacyCameraTransform(Entity& entity, IComponent& component)
{
    if (entity.hasComponent<Transform>()) return;
    auto& camera = static_cast<OrthoCameraComponent&>(component);
    if (Transform* transform = addTransformIfMissing(entity)) {
        transform->position = {camera.positionX, camera.positionY, 0.0f};
        transform->rotation = math::FQuaternion::fromAxisAngle(
            math::FVector3(0.0f, 0.0f, 1.0f), camera.rotationRadians);
    }
}

} // namespace

ComponentRegistryResult registerEntity2DComponents(ComponentRegistry& registry)
{
    if (auto result = registerSceneComponent<TilemapComponent>(
            registry, "TilemapComponent", "Tilemap", "2D",
            migrateLegacyTilemapTransform);
        !result) {
        return result;
    }
    if (auto result = registerSceneComponent<SpriteComponent>(
            registry, "SpriteComponent", "Sprite", "2D",
            migrateLegacySpriteTransform);
        !result) {
        return result;
    }
    if (auto result = registerSceneComponent<SpriteAnimationComponent>(
            registry, "SpriteAnimationComponent", "Sprite Animation", "2D");
        !result) {
        return result;
    }
    return registerSceneComponent<OrthoCameraComponent>(
        registry,
        "OrthoCameraComponent",
        "Orthographic Camera",
        "2D",
        migrateLegacyCameraTransform,
        ComponentMultiplicity::Multiple);
}

} // namespace ayt::entity
