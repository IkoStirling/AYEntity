#pragma once
// AYTilemapAnimationTickSystem.h — CM-3 (2026-08-11): empty shell.
//
// Reserved slot (priority 460) for the per-frame tilemap animation
// table tick (e.g. water/foliage tile-id cycling driven by an
// animation table on the AY2D side). Mirrors the GBufferPass empty
// shell precedent — the priority table (§3.3) stays authoritative.

#include <IAYEntity.h>

namespace ayt::entity
{

class TilemapAnimationTickSystem : public ISystem {
public:
    const char* getName() const override { return "TilemapAnimationTickSystem"; }
    void onStart() override {}
    void onUpdate(float /*dt*/) override {}

    static constexpr int kPriority = 460;
};

void registerTilemapAnimationTickSystem();

} // namespace ayt::entity
