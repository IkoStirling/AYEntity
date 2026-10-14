#include <AYEntity/PerspectiveCameraUpdateSystem.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <AYEntity/World.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYGameLoop.h>
#include <AYEntity/CameraRuntime.h>
#include <AYEntity/components/PerspectiveCameraComponent.h>

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
        const int32_t viewport = renderer->activeViewportId();
        if (!evaluatePerspectiveCamera3D(*owner, renderer->viewportAspect(),
            game::GameLoop::instance().getInterpolationFactor(), frame,viewport)) {
            if (auto* system = owner->findSystemByName("CameraRuntimeSystem"))
                static_cast<CameraRuntimeSystem*>(system)->resetView(viewport,false);
            return false;
        }
        const auto selected = selectPerspectiveCamera3D(*owner,viewport);
        if (auto* system = owner->findSystemByName("CameraRuntimeSystem"))
            frame = static_cast<CameraRuntimeSystem*>(system)->present(frame,viewport,false,selected.camera->blendDuration);
        output = {frame.view, frame.projection, frame.position,
                  frame.identity, frame.cutGeneration,frame.layerMask};
        return true;
    });
}
void registerPerspectiveCameraUpdateSystem() {
    World::instance().registerSystem<PerspectiveCameraUpdateSystem>(
        PerspectiveCameraUpdateSystem::kPriority);
}
}
