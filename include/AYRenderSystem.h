#pragma once

#include <IAYEntity.h>
#include <AYRenderScene.h>

namespace ayt::entity
{

class RenderSystem : public ISystem {
public:
    const char* getName() const override { return "RenderSystem"; }
    void onStart() override;
    void onUpdate(float dt) override;

private:
    void buildRenderScene(ayt::render::RenderScene& scene);

    bool _started = false;
};

} // namespace ayt::entity
