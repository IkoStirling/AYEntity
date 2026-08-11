#pragma once
// AYOrthoCameraUpdateSystem.h — CM-3 (2026-08-11): 2D ortho camera
// driver. Priority 405 — registers BEFORE RenderSystem (500) so its
// scene-builder callback runs first in the setSceneBuilder chain and
// overwrites the default perspective camera with the primary
// OrthoCameraComponent's ortho view/projection before the 2D/3D
// render systems submit.
//
// Known mixed-3D caveat: RendererSubSystem::renderScenePass sets the
// default perspective camera before running the builder chain, so a
// primary 2D camera REPLACES it for the whole frame — 3D Transparent
// sortKey keeps using mainCameraPosition() (last look-at eye), a
// documented limitation of mixed 2D/3D scenes in one frame.

#include <IAYEntity.h>

namespace ayt::render
{
class RenderScene;
}

namespace ayt::entity
{

class OrthoCameraUpdateSystem : public ISystem {
public:
    const char* getName() const override { return "OrthoCameraUpdateSystem"; }
    void onStart() override;
    void onUpdate(float /*dt*/) override {}

    static constexpr int kPriority = 405;

    // Exposed for tests / debug only. Not part of the ISystem contract
    // (mirrors AnimationSystem::kPriority).
    void buildCamera(ayt::render::RenderScene& scene);

private:
    bool _started = false;
};

void registerOrthoCameraUpdateSystem();

} // namespace ayt::entity
