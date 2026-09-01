#include "AYEntity/TilemapStreamingSystem.h"

#include "AYEntity.h"
#include "AYEntity/TilemapVisibilityRuntime.h"
#include "AYEntity/World.h"
#include "AYEntity/components/OrthoCameraComponent.h"

namespace ayt::entity
{

void TilemapStreamingSystem::onStart()
{
    onUpdate(0.0f);
}

void TilemapStreamingSystem::onUpdate(float)
{
    TilemapCameraVisibility state{};
    World& world = World::instance();
    for (Entity* entity : world.query<OrthoCameraComponent>()) {
        if (entity == nullptr) continue;
        const OrthoCameraComponent* camera =
            entity->getComponent<OrthoCameraComponent>();
        if (camera == nullptr || !camera->isPrimary || camera->viewSize <= 0.0f) {
            continue;
        }
        const float halfH = camera->viewSize * 0.5f;
        const float halfW = halfH * camera->viewportAspectOr();
        state.valid = true;
        state.minX = camera->positionX - halfW;
        state.minY = camera->positionY - halfH;
        state.maxX = camera->positionX + halfW;
        state.maxY = camera->positionY + halfH;
        state.layerMask = camera->layerMask;
        break;
    }
    TilemapVisibilityRuntime::instance().publish(state);
}

void registerTilemapStreamingSystem()
{
    World::instance().registerSystem<TilemapStreamingSystem>(
        TilemapStreamingSystem::kPriority);
}

} // namespace ayt::entity
