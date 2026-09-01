#pragma once
// AYEntity/TilemapStreamingSystem.h — visibility hand-off for the production
// 2D ECS path. Priority 430 publishes the primary orthographic camera rectangle
// before tile animation (460) and rendering (510). GPU chunk residency itself
// is owned by TilemapRenderSystem, where mesh handles can be destroyed safely.

#include <AYEntity/IEntity.h>

namespace ayt::entity
{

class TilemapStreamingSystem : public ISystem {
public:
    const char* getName() const override { return "TilemapStreamingSystem"; }
    void onStart() override;
    void onUpdate(float /*dt*/) override;

    static constexpr int kPriority = 430;
};

void registerTilemapStreamingSystem();

} // namespace ayt::entity
