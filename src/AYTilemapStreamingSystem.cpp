#include "AYEntity/TilemapStreamingSystem.h"

#include "AYEntity.h"
#include "AYEntity/OrthoCameraSelection.h"
#include "AYEntity/TilemapVisibilityRuntime.h"
#include "AYEntity/World.h"
#include "AYEntity/components/OrthoCameraComponent.h"
#include "AYRenderer/RendererSubSystem.h"

#include <cmath>

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
    const SelectedOrthoCamera2D selected = selectOrthoCamera2D(world);
    if (selected) {
        const ayt::render::RendererSubSystem* renderer =
            ayt::render::RendererSubSystem::findRegistered();
        const float aspect = renderer != nullptr
            ? renderer->viewportAspect() : selected.camera->viewportAspectOr();
        const math::FVector2 viewHalf =
            selected.camera->visibleHalfExtents(aspect);
        const float entityAngle = selected.transform->rotation.toEulerAngles().z;
        const float angle = selected.camera->worldRotation(entityAngle);
        const float c = std::fabs(std::cos(angle));
        const float s = std::fabs(std::sin(angle));
        const float halfW = c * viewHalf.x + s * viewHalf.y;
        const float halfH = s * viewHalf.x + c * viewHalf.y;
        const auto center = selected.camera->worldCenter(
            selected.transform->position, entityAngle);
        const float cameraX = center.x;
        const float cameraY = center.y;
        state.valid = true;
        state.minX = cameraX - halfW;
        state.minY = cameraY - halfH;
        state.maxX = cameraX + halfW;
        state.maxY = cameraY + halfH;
        state.layerMask = selected.camera->layerMask;
    }
    TilemapVisibilityRuntime::instance().publish(state);
}

void registerTilemapStreamingSystem()
{
    World::instance().registerSystem<TilemapStreamingSystem>(
        TilemapStreamingSystem::kPriority);
}

} // namespace ayt::entity
