#include <AYEntity/PerspectiveCameraUpdateSystem.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <AYEntity/World.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYGameLoop.h>

namespace ayt::entity {
void PerspectiveCameraUpdateSystem::onStart() {
    auto* renderer = render::RendererSubSystem::findRegistered();
    if (!renderer) return;
    World* owner = &World::instance();
    renderer->setSceneCameraProviderForOwner(owner, [owner](render::SceneCamera3D& output) {
        if (&World::instance() != owner) return false;
        const auto* renderer = render::RendererSubSystem::findRegistered();
        if (!renderer) return false;
        PerspectiveCameraFrame frame;
        if (!evaluatePerspectiveCamera3D(*owner, renderer->viewportAspect(),
            game::GameLoop::instance().getInterpolationFactor(), frame)) return false;
        output = {frame.view, frame.projection, frame.position,
                  frame.identity, frame.cutGeneration};
        return true;
    });
}
void registerPerspectiveCameraUpdateSystem() {
    World::instance().registerSystem<PerspectiveCameraUpdateSystem>(
        PerspectiveCameraUpdateSystem::kPriority);
}
}
