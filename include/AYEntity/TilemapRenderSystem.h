#pragma once
// AYEntity/TilemapRenderSystem.h — CM-3 (2026-08-11): 2D tilemap draw
// submission. Priority 510 (after camera/streaming/animation @405/430/460).
// One indexed mesh and DrawItem are emitted per visible chunk; static meshes
// remain resident under a component-configured LRU cap.
//
// Lazy-load contract (L-16): tile data is loaded on first use via
// AYResourceManager::load<IAYTilemap>(tilemapPath); load failure
// produces a skip (entity invisible) with a startup-only stderr log —
// never an exception. GPU assets (texture + material) are cached
// path-keyed; the material is created once from the embedded
// kTilemapPhoskiaSource and the texture bound to "albedoMap".
//
#include <AYEntity/IEntity.h>

#include <AYRenderer/RenderScene.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::resource
{
class ITilemap;
class IAtlas;
}

namespace ayt::entity
{

class TilemapRenderSystem : public ISystem {
public:
    struct FrameStats {
        uint64_t cellsVisited = 0;
        uint32_t visibleChunks = 0;
        uint32_t drawItemsEmitted = 0;
        uint32_t chunkMeshesBuilt = 0;
        uint32_t residentChunkMeshes = 0;
        uint32_t chunkMeshesEvicted = 0;
    };

    ~TilemapRenderSystem() override = default;
    const char* getName() const override { return "TilemapRenderSystem"; }
    void onStart() override;
    void onUpdate(float /*dt*/) override {}

    static constexpr int kPriority = 510;

    // Exposed for tests / debug only. Not part of the ISystem contract.
    void buildRenderScene(ayt::render::RenderScene& scene);
    [[nodiscard]] const FrameStats& lastFrameStats() const noexcept {
        return _lastStats;
    }

private:
    struct CachedChunkMesh {
        ayt::render::MeshHandle mesh;
        uint64_t animationRevision = 0;
        uint64_t lastUsedFrame = 0;
    };

    struct CachedTilemapResources {
        std::shared_ptr<ayt::resource::ITilemap> tilemap;  // null = not loaded / failed
        std::shared_ptr<ayt::resource::IAtlas> atlas;
        ayt::render::TextureHandle  texture;               // invalid = not loaded / failed
        std::array<ayt::render::MaterialHandle, 4> materials{};
        std::unordered_map<uint64_t, CachedChunkMesh> chunks;
    };
    std::unordered_map<std::string, CachedTilemapResources> _cache;

    // Payload borrow contract (same as SceneLights): the payload
    // buffer must outlive render()'s synchronous consumption. Owned
    // here, cleared at the top of each build, filled once (reserve
    // before filling so &_payloads.back() is stable).
    std::vector<ayt::render::DrawPayload2D> _payloads;

    FrameStats _lastStats{};
    uint64_t _frameIndex = 0;
    bool _started = false;
};

void registerTilemapRenderSystem();

} // namespace ayt::entity
