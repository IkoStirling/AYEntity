#pragma once
// AYEntity/TilemapRenderSystem.h — CM-3 (2026-08-11): 2D tilemap draw
// submission. Priority 510 (after OrthoCameraUpdateSystem@405 and the
// streaming/animation shells @430/@460). One draw per tile — the
// Forward2DOpaquePass lane (payload != nullptr items) owns them; the
// unit quad + kTilemapPhoskiaSource material are the only GPU assets.
//
// Lazy-load contract (L-16): tile data is loaded on first use via
// AYResourceManager::load<IAYTilemap>(tilemapPath); load failure
// produces a skip (entity invisible) with a startup-only stderr log —
// never an exception. GPU assets (texture + material) are cached
// path-keyed; the material is created once from the embedded
// kTilemapPhoskiaSource and the texture bound to "albedoMap".
//
// Culling note: chunked visibility culling belongs to
// TilemapStreamingSystem (empty shell today) — this system submits
// every tile of every visible, valid entity. A 60x60 map costs ~3600
// draws; batching is a Phase 6 budget item.

#include <AYEntity/IEntity.h>

#include <AYRenderer/RenderScene.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::resource
{
class ITilemap;
}

namespace ayt::entity
{

class TilemapRenderSystem : public ISystem {
public:
    const char* getName() const override { return "TilemapRenderSystem"; }
    void onStart() override;
    void onUpdate(float /*dt*/) override {}

    static constexpr int kPriority = 510;

    // Exposed for tests / debug only. Not part of the ISystem contract.
    void buildRenderScene(ayt::render::RenderScene& scene);

private:
    struct CachedTilemapResources {
        std::shared_ptr<ayt::resource::ITilemap> tilemap;  // null = not loaded / failed
        ayt::render::TextureHandle  texture;               // invalid = not loaded / failed
        ayt::render::MaterialHandle material;              // invalid = not created / failed
    };
    std::unordered_map<std::string, CachedTilemapResources> _cache;

    // Payload borrow contract (same as SceneLights): the payload
    // buffer must outlive render()'s synchronous consumption. Owned
    // here, cleared at the top of each build, filled once (reserve
    // before filling so &_payloads.back() is stable).
    std::vector<ayt::render::DrawPayload2D> _payloads;

    // Shared unit quad, created once on first build. Created per frame
    // used to leak one GpuMesh (2 bgfx buffers) every frame until the
    // device ran out of resources mid-session.
    ayt::render::MeshHandle _quad;

    bool _started = false;
};

void registerTilemapRenderSystem();

} // namespace ayt::entity
