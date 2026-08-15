// AYTilemapRenderSystem.cpp — CM-3 (2026-08-11).
//
// Draw pipeline per entity:
//   1. Lazy-load the tilemap via AYResourceManager::load<IAYTilemap>
//      (L-16 marker — failure => skip, no exception).
//   2. Cache the atlas texture + the kTilemapPhoskiaSource material
//      (path-keyed; material created once, albedoMap bound once).
//   3. One DrawItem per tile: world = entity.position +
//      cellCenterWorld(col, row) (mirror of ayt::ay2d::cellToWorld,
//      cellOrigin {0,0}), scale = tile size; payload carries the
//      tile's atlas UV (mirror of ayt::ay2d::tileUV, gutter=0).
//
// Row-major submission order is the author order for equal sort keys
// (the Forward2DOpaquePass stable-sorts by packedSortKey).

#include "AYEntity/TilemapRenderSystem.h"

#include "AYEntity/2DUvMath.h"
#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
#include "AYRenderer/RendererSubSystem.h"
#include "AYEntity/TilemapAnimationRuntime.h"
#include "AYRenderer/TilemapShaderSources.h"
#include "AYEntity/World.h"

#include "AYEntity/components/TransformComponent.h"
#include "AYEntity/components/TilemapComponent.h"

#include <AYResource/assetsDefs/ITilemap.h>
#include <AYResource/ResourceManager.h>

#include <AYMath/MathTransform.h>

#include <cstdio>

namespace ayt::entity
{

namespace
{

uint32_t tileIdAt(const ayt::resource::ITilemap& map, uint32_t col, uint32_t row)
{
    const uint32_t cols = map.getCols();
    if (cols == 0 || row >= map.getRows() || col >= cols) {
        return map.getDefaultTileId();
    }
    const uint32_t idx = row * cols + col;
    if (idx >= map.getTileIdCount()) {
        return map.getDefaultTileId();
    }
    if (map.getPackMode() == ayt::resource::TilemapPackMode::Narrow16) {
        const uint16_t* ids = map.getTileIds16();
        return ids ? static_cast<uint32_t>(ids[idx]) : map.getDefaultTileId();
    }
    const uint32_t* ids = map.getTileIds32();
    return ids ? ids[idx] : map.getDefaultTileId();
}

} // namespace

void TilemapRenderSystem::onStart()
{
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        std::fprintf(stderr,
                     "[TilemapRenderSystem] RendererSubSystem not registered; "
                     "tilemap draws will not be submitted.\n");
        return;
    }
    rss->setSceneBuilder([this](ayt::render::RenderScene& scene) {
        buildRenderScene(scene);
    });
    _started = true;
    std::fprintf(stderr, "[TilemapRenderSystem] scene builder registered\n");
}

void TilemapRenderSystem::buildRenderScene(ayt::render::RenderScene& scene)
{
    static uint32_t s_frameIndex = 0;
    const uint32_t frameIndex = s_frameIndex++;

    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        return;
    }
    ayt::render::Renderer& renderer = rss->renderer();

    _payloads.clear();

    ayt::render::MeshHandle quad = renderer.createUnitQuad();
    if (!quad.isValid()) {
        return;
    }

    // Per-entity (world, payload) pairs collected first; the payload
    // buffer is filled once with a reserve so every item.payload
    // pointer stays stable across the loop (borrow contract: the
    // buffer outlives render()'s synchronous consumption).
    struct TileEntry {
        ayt::render::DrawItem      item;
        ayt::render::DrawPayload2D payload;
    };
    std::vector<TileEntry> entries;

    World& world = World::instance();
    for (Entity* entity : world.query<Transform, TilemapComponent>()) {
        if (entity == nullptr) {
            continue;
        }
        Transform*       transform = entity->getComponent<Transform>();
        TilemapComponent* tm       = entity->getComponent<TilemapComponent>();
        if (transform == nullptr || tm == nullptr || !tm->visible
            || !tm->isValid()) {
            continue;
        }

        const std::string key = tm->tilemapPath + "|" + tm->atlasTexturePath;
        CachedTilemapResources& resources = _cache[key];

        // L-16: lazy-load on first use; a failed path latches null so
        // we do not re-query ResourceManager every frame.
        if (!resources.tilemap) {
            resources.tilemap = ayt::resource::ResourceManager::instance()
                                    .load<ayt::resource::ITilemap>(tm->tilemapPath);
            if (!resources.tilemap && frameIndex < 5) {
                std::fprintf(stderr, "[TilemapRenderSystem] load<ITilemap> failed: '%s'\n",
                             tm->tilemapPath.c_str());
            }
        }
        if (!resources.texture.isValid() && !tm->atlasTexturePath.empty()) {
            resources.texture = renderer.loadTexture(tm->atlasTexturePath);
            if (!resources.texture.isValid() && frameIndex < 5) {
                std::fprintf(stderr, "[TilemapRenderSystem] loadTexture failed: '%s'\n",
                             tm->atlasTexturePath.c_str());
            }
        }
        if (!resources.material.isValid() && !tm->atlasTexturePath.empty()) {
            resources.material = renderer.createMaterialFromPhoskia(
                ayt::render::kTilemapPhoskiaSource, tm->atlasTexturePath);
            if (resources.material.isValid()) {
                renderer.setMaterialTexture(resources.material, "albedoMap",
                                            resources.texture);
            } else if (frameIndex < 5) {
                std::fprintf(stderr,
                             "[TilemapRenderSystem] material compile failed "
                             "(shaderc missing?)\n");
            }
        }

        // Material validity alone is not enough: createMaterialFromPhoskia
        // only compiles the shader, so a tilemap whose atlas texture
        // failed to load would submit items with no albedo. A broken
        // texture must produce zero items (CM-3 lazy-load failure
        // contract). A texture-less tilemap (empty atlasTexturePath)
        // already fell through via the material gate — same outcome.
        if (!resources.tilemap || !resources.texture.isValid()
            || !resources.material.isValid()) {
            continue;
        }

        // CM-5: animated source tile ids resolve through the runtime.
        // The tick system (460) refreshes resolved[] every frame before
        // this builder runs (510, same thread — the lane runs with
        // setRenderThreadEnabled(false)). A path with no runtime entry
        // (tick system not registered, or load failed) renders fully
        // static — the AY2D "no table == static" contract.
        TilemapAnimationRuntimeEntry* animEntry =
            TilemapAnimationRuntime::instance().find(tm->tilemapPath);

        const uint32_t cols = resources.tilemap->getCols();
        const uint32_t rows = resources.tilemap->getRows();
        if (cols == 0 || rows == 0) {
            continue;
        }

        // Dense-atlas grid (mirror of the AY2D convention): atlas
        // extent is implied as tile size x grid count, gutter = 0.
        AtlasGridDesc grid;
        grid.tilesPerRow      = static_cast<uint32_t>(
            tm->atlasTilesPerRow > 0 ? tm->atlasTilesPerRow : 1);
        grid.tilesPerColumn   = static_cast<uint32_t>(
            tm->atlasTilesPerColumn > 0 ? tm->atlasTilesPerColumn : 1);
        grid.tileWidthTexels  = resources.tilemap->getTileWidth();
        grid.tileHeightTexels = resources.tilemap->getTileHeight();

        const float tileW = static_cast<float>(resources.tilemap->getTileWidth());
        const float tileH = static_cast<float>(resources.tilemap->getTileHeight());
        const uint32_t sortKey = drawSortKey(tm->layer, tm->sortingKey);

        for (uint32_t row = 0; row < rows; ++row) {
            for (uint32_t col = 0; col < cols; ++col) {
                uint32_t tileId = tileIdAt(*resources.tilemap, col, row);
                if (animEntry != nullptr) {
                    tileId = TilemapAnimationRuntime::resolve(*animEntry,
                                                              tileId);
                }
                const TileUvQuad uv = tileUvQuad(tileId, grid);

                TileEntry entry;
                entry.payload.sourceRectMin = ayt::math::FVector2(uv.uMin, uv.vMin);
                entry.payload.sourceRectMax = ayt::math::FVector2(uv.uMax, uv.vMax);
                entry.payload.tintRGBA      = ayt::math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
                entry.payload.flip          = 0;
                entry.payload.packedSortKey = sortKey;

                // Mirror of ayt::ay2d::cellToWorld (cellOrigin {0,0}):
                // tile center sits at entity origin + ((col+0.5)*tileW,
                // (row+0.5)*tileH) — the quad is centered on that point
                // and scaled to the tile size.
                const ayt::math::FVector2 center =
                    cellCenterWorld(col, row, tileW, tileH);
                entry.item.mesh     = quad;
                entry.item.material = resources.material;
                entry.item.world    = ayt::math::Transform::getMatrix(
                    ayt::math::FVector3(transform->position.x + center.x,
                                        transform->position.y + center.y,
                                        transform->position.z),
                    ayt::math::FQuaternion::identity(),
                    ayt::math::FVector3(tileW, tileH, 1.0f));
                entries.push_back(entry);
            }
        }
    }

    _payloads.reserve(entries.size());
    for (TileEntry& entry : entries) {
        _payloads.push_back(entry.payload);
        entry.item.payload = &_payloads.back();
        scene.add(entry.item);
    }
}

void registerTilemapRenderSystem()
{
    World::instance().registerSystem<TilemapRenderSystem>(
        TilemapRenderSystem::kPriority);
}

} // namespace ayt::entity
