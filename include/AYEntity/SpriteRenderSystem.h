#pragma once
// AYEntity/SpriteRenderSystem.h — CM-3 (2026-08-11): 2D sprite draw
// submission. Priority 510 (same as TilemapRenderSystem; the
// bootstrapModule registration order decides the chain order, pinned
// by unittest). One draw per sprite with payload sourceRect/tint/flip.
//
// Ordering hard rule (design.md §7.4): submitted in ascending
// packedSortKey (layer<<24 | sortingKey&0xFFFFFF) via std::stable_sort
// — equal keys keep author order. The Forward2DOpaquePass sorts the
// same key ascending, so item order here IS final draw order.
//
// Culling: view-frustum AABB test against the primary
// OrthoCameraComponent (world rect = position ± viewSize/2,
// viewSize*aspect/2 — zoom does not change the world extent, mirror
// of AY2D). No primary camera -> fail-open (submit everything).

#include <AYEntity/IEntity.h>

#include <AYRenderer/RenderScene.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::entity
{

class SpriteRenderSystem : public ISystem {
public:
    const char* getName() const override { return "SpriteRenderSystem"; }
    void onStart() override;
    void onUpdate(float /*dt*/) override {}

    static constexpr int kPriority = 510;

    // Exposed for tests / debug only. Not part of the ISystem contract.
    void buildRenderScene(ayt::render::RenderScene& scene);

private:
    struct CachedSpriteResources {
        ayt::render::TextureHandle  texture;   // invalid = not loaded / failed
        ayt::render::MaterialHandle material;  // invalid = not created / failed
    };
    std::unordered_map<std::string, CachedSpriteResources> _cache;

    std::vector<ayt::render::DrawPayload2D> _payloads;

    // Shared unit quad, created once on first build. Created per frame
    // used to leak one GpuMesh (2 bgfx buffers) every frame until the
    // device ran out of resources mid-session.
    ayt::render::MeshHandle _quad;

    bool _started = false;
};

void registerSpriteRenderSystem();

} // namespace ayt::entity
