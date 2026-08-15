#pragma once
// AYEntity/TilemapStreamingSystem.h — CM-3 (2026-08-11): empty shell.
//
// Reserved slot (priority 430, before TilemapAnimationTickSystem@460
// and the render systems@510) for the chunk-source / visibility
// streaming pipeline. The tilemap data pipeline lives on the AY2D
// side; this system will own chunk loading + culling hand-off to
// TilemapRenderSystem in a future PR. Mirrors the GBufferPass empty
// shell precedent — the priority table (§3.3) stays authoritative.

#include <AYEntity/IEntity.h>

namespace ayt::entity
{

class TilemapStreamingSystem : public ISystem {
public:
    const char* getName() const override { return "TilemapStreamingSystem"; }
    void onStart() override {}
    void onUpdate(float /*dt*/) override {}

    static constexpr int kPriority = 430;
};

void registerTilemapStreamingSystem();

} // namespace ayt::entity
