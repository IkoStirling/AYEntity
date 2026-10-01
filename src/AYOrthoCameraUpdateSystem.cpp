// AYOrthoCameraUpdateSystem.cpp — CM-3 (2026-08-11).

#include "AYEntity/OrthoCameraUpdateSystem.h"

#include "AYEntity.h"
#include "AYEntity/OrthoCameraSelection.h"
#include "AYEntity/components/OrthoCameraComponent.h"
#include "AYRenderer/RendererSubSystem.h"
#include "AYEntity/World.h"

#include <AYGameLoop.h>

#include <cstdio>

namespace ayt::entity
{

void OrthoCameraUpdateSystem::onStart()
{
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        std::fprintf(stderr,
                     "[OrthoCameraUpdateSystem] RendererSubSystem not registered; "
                     "2D overlay camera builder was not registered.\n");
        return;
    }
    World* owner = &World::instance();
    rss->addSceneBuilderForOwner(
        owner, [this, owner](ayt::render::RenderScene& scene) {
            if (&World::instance() == owner) buildCamera(scene);
        });
    _started = true;
    std::fprintf(stderr, "[OrthoCameraUpdateSystem] world-owned scene builder registered\n");
}

void OrthoCameraUpdateSystem::buildCamera(ayt::render::RenderScene& scene)
{
    World& world = World::instance();
    const SelectedOrthoCamera2D selected = selectOrthoCamera2D(world);
    if (!selected) return;
    const ayt::render::RendererSubSystem* renderer =
        ayt::render::RendererSubSystem::findRegistered();
    const float aspect = renderer != nullptr
        ? renderer->viewportAspect() : selected.camera->viewportAspectOr();
    const float interpolationAlpha =
        ayt::game::GameLoop::instance().getInterpolationFactor();
    const math::FVector3 position =
        selected.transform->interpolatedPosition(interpolationAlpha);
    const math::FQuaternion rotation =
        selected.transform->interpolatedRotation(interpolationAlpha);
    const float angle = rotation.toEulerAngles().z;
    const auto center = selected.camera->worldCenter(position, angle);
    scene.setOverlayCamera2D(
        selected.camera->viewMatrix(
            center.x, center.y, selected.camera->worldRotation(angle)),
        selected.camera->projectionMatrix(aspect),
        selected.camera->layerMask);
}

void registerOrthoCameraUpdateSystem()
{
    World::instance().registerSystem<OrthoCameraUpdateSystem>(
        OrthoCameraUpdateSystem::kPriority);
}

} // namespace ayt::entity
