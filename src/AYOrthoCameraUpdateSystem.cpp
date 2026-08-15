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
                     "2D camera will not drive the main camera.\n");
        return;
    }
    rss->setSceneBuilder([this](ayt::render::RenderScene& scene) {
        buildCamera(scene);
    });
    _started = true;
    std::fprintf(stderr, "[OrthoCameraUpdateSystem] scene builder registered\n");
}

void OrthoCameraUpdateSystem::buildCamera(ayt::render::RenderScene& /*scene*/)
{
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        return;
    }

    // The builder chain runs AFTER renderScenePass's default
    // perspective camera setup, so setMainCamera here wins the frame.
    World& world = World::instance();
    for (Entity* entity : world.query<OrthoCameraComponent>()) {
        if (entity == nullptr) {
            continue;
        }
        OrthoCameraComponent* cam = entity->getComponent<OrthoCameraComponent>();
        if (cam == nullptr || !cam->isPrimary) {
            continue;
        }
        rss->renderer().setMainCamera(cam->viewMatrix(), cam->projectionMatrix());
        return;  // first primary camera wins
    }
}

void registerOrthoCameraUpdateSystem()
{
    World::instance().registerSystem<OrthoCameraUpdateSystem>(
        OrthoCameraUpdateSystem::kPriority);
}

} // namespace ayt::entity
