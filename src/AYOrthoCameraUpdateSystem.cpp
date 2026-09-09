// AYOrthoCameraUpdateSystem.cpp — CM-3 (2026-08-11).

#include "AYEntity/OrthoCameraUpdateSystem.h"

#include "AYEntity.h"
#include "AYRenderer/RendererSubSystem.h"
#include "AYEntity/World.h"

#include "AYEntity/components/OrthoCameraComponent.h"

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
    for (Entity* entity : world.query<OrthoCameraComponent>()) {
        if (entity == nullptr) {
            continue;
        }
        OrthoCameraComponent* cam = entity->getComponent<OrthoCameraComponent>();
        if (cam == nullptr || !cam->isPrimary) {
            continue;
        }
        scene.setOverlayCamera2D(cam->viewMatrix(), cam->projectionMatrix(),
                                 cam->layerMask);
        return;  // first primary camera wins
    }
}

void registerOrthoCameraUpdateSystem()
{
    World::instance().registerSystem<OrthoCameraUpdateSystem>(
        OrthoCameraUpdateSystem::kPriority);
}

} // namespace ayt::entity
