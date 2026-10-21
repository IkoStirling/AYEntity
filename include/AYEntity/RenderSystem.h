#pragma once

#include <AYEntity/IEntity.h>
#include <AYRenderer/RenderScene.h>
#include <memory>

namespace ayt::entity
{

class RenderSystem : public ISystem {
public:
    RenderSystem();
    ~RenderSystem() override;
    const char* getName() const override { return "RenderSystem"; }
    void onStart() override;
    void onUpdate(float dt) override;

private:
    struct ResourceCache;
    std::unique_ptr<ResourceCache> _resources;
    void buildRenderScene(ayt::render::RenderScene& scene);

    bool _started = false;
};

void registerRenderSystem();

} // namespace ayt::entity
