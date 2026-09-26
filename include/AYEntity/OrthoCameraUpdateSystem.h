#pragma once
// AYEntity/OrthoCameraUpdateSystem.h — CM-3 (2026-08-11): 2D ortho camera
// driver. Priority 405 — registers BEFORE RenderSystem (500) so its
// World-owned scene-builder callback records the primary
// OrthoCameraComponent as RenderScene's independent 2D overlay camera.
// The Renderer main perspective camera remains available to every 3D pass.
// Camera placement samples Transform's Sim-to-Present interpolation alpha so
// fixed-step camera motion remains smooth at the presentation refresh rate.

#include <AYEntity/IEntity.h>

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
