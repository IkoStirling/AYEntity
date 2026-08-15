#pragma once

#include <AYEntity/IEntity.h>
#include <AYRenderer/RenderScene.h>

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
