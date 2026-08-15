#pragma once
// AYEntity/TilemapAnimationTickSystem.h — CM-3 (2026-08-11): shell, filled
// CM-5 (2026-08-12).
//
// Per-frame tilemap animation table tick (water/foliage tile-id cycling).
// Priority 460 — after the streaming shell (430), before the render
// systems (510), so every frame's resolve buffer is fresh when the scene
// is built. Mirrors the GBufferPass empty-shell precedent (§3.3): the
// priority table stays authoritative.

#include <AYEntity/IEntity.h>

namespace ayt::entity
{

class TilemapAnimationTickSystem : public ISystem {
public:
    const char* getName() const override { return "TilemapAnimationTickSystem"; }
    void onStart() override {}
    void onUpdate(float dt) override;

    static constexpr int kPriority = 460;
};

void registerTilemapAnimationTickSystem();

} // namespace ayt::entity
