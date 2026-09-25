#pragma once
// AYEntity/components/AYEntity/components/AYEntity/components/AYEntity/components/TilemapComponent.h — CM-3 (2026-08-11): 2D tilemap placement metadata.
//
// Holds ONLY path + placement metadata — no tile-id array, no GPU
// handles. Tile data is loaded lazily by TilemapRenderSystem at render
// time via AYResourceManager::load<IAYTilemap>(path) (lazy-load marker:
// L-16); on failure the system skips the entity (isValid()==false
// marker, no exception). GPU-side resources (texture + material) are
// cached by the render system keyed on atlasTexturePath.
//
// Dependency-direction lock: AYEntity must not depend on AY2D, so the
// tile->UV math (mirror of ayt::ay2d::tileUV) and the tile->world math
// (mirror of ayt::ay2d::cellToWorld) live in AYEntity/2DUvMath.h; the
// unittest cross-asserts them against the real AY2D headers.

#include <AYCore.h>
#include <AYEntity/IEntity.h>

#include <cstdint>
#include <string>

namespace ayt::entity
{

#define AY_CURRENT_CLASS TilemapComponent
struct TilemapComponent : public IComponent {
    const char* getName() const override { return "TilemapComponent"; }

    // Paths may be absolute or portable references below the asset root
    // (for example tilemaps/restaurant.aytilemap). Path fields declared via
    // the AY_PROPERTY macro below. The expansion emits `Type name;` and
    // registers serializer metadata; keep these declarations macro-only.
    AY_PROPERTY(std::string, tilemapPath, kAttrSerialize)
    AY_PROPERTY(std::string, atlasTexturePath, kAttrSerialize)
    // Optional atlas-aligned WorldLit2D surface maps. Empty paths use the
    // renderer's flat-normal/white fallbacks.
    AY_PROPERTY(std::string, normalTexturePath, kAttrSerialize)
    AY_PROPERTY(std::string, roughnessTexturePath, kAttrSerialize)
    AY_PROPERTY(std::string, emissiveTexturePath, kAttrSerialize)
    // Atlas grid layout. The atlas texture is assumed to be a dense
    // grid of tiles of size (IAYTilemap::getTileWidth() x
    // getTileHeight()) with zero gutter — the AY2D dense-atlas
    // convention (ATLAS origin-bottom-left, tile-id 0 at bottom-left).
    //
    // M2 (lh-rh-split-entity audit 2026-08-24): the AY2D authoring
    // convention is intentionally V-bottom-left for tilemap atlas
    // data, even though the AYMath engine-wide default is V-top
    // (CoordinateConvention.h::textureVOrigin). The override lives
    // HERE, in the AY2D dense-atlas contract — UV math in
    // AYEntity/2DUvMath.h:41-69 already compensates. A future
    // render-backend V-origin flip must NOT change this atlas
    // convention without also updating 2DUvMath + every authored
    // .ayatlas asset. See design.md §3.2.
    AY_PROPERTY(int32_t, atlasTilesPerRow, kAttrSerialize)
    AY_PROPERTY(int32_t, atlasTilesPerColumn, kAttrSerialize)
    // Optional formal atlas metadata asset. When set, `.ayatlas` supplies the
    // texture path/grid/filter and supersedes the legacy fields above. Keeping
    // the legacy fields preserves existing scenes and raw-texture workflows.
    AY_PROPERTY(std::string, atlasPath, kAttrSerialize)
    // Chunk/residency controls. The renderer clamps chunk dimensions to 1..64
    // so uint16 indexed meshes remain valid. residentChunkLimit <= 0 uses 256.
    AY_PROPERTY(int32_t, chunkColumns, kAttrSerialize)
    AY_PROPERTY(int32_t, chunkRows, kAttrSerialize)
    AY_PROPERTY(int32_t, residentChunkLimit, kAttrSerialize)
    AY_PROPERTY(int32_t, prefetchMarginChunks, kAttrSerialize)
    // 0=Nearest, 1=Linear, 2=4-tap, 3=9-tap. Unknown values clamp to Linear.
    AY_PROPERTY(int32_t, samplingQuality, kAttrSerialize)
    // 2D draw ordering: layer (high byte of packedSortKey) wins, then
    // sortingKey (low 24 bits). See design.md §7.4 / DrawPayload2D.
    AY_PROPERTY(int32_t, layer, kAttrSerialize)
    AY_PROPERTY(int32_t, sortingKey, kAttrSerialize)
    // 0 = legacy SceneOverlay (default), 1 = deferred WorldLit2D.
    AY_PROPERTY(int32_t, renderDomain, kAttrSerialize)
    AY_PROPERTY(float, metallic, kAttrSerialize)
    AY_PROPERTY(float, roughness, kAttrSerialize)
    AY_PROPERTY(float, ambientOcclusion, kAttrSerialize)
    AY_PROPERTY(float, emissiveStrength, kAttrSerialize)
    AY_PROPERTY(float, alphaCutoff, kAttrSerialize)
    AY_PROPERTY(bool, invertNormalY, kAttrSerialize)
    // WorldLit-only caster control. SceneOverlay never enters ShadowPass.
    AY_PROPERTY(bool, castShadow, kAttrSerialize)

    // Runtime-only (not serialized): render skip flag.
    bool visible = true;

    TilemapComponent() {
        // AY_PROPERTY expands to `Type name;` with no initializer —
        // explicit ctor assignment is required (AYEntity/components/AYEntity/components/AYEntity/components/MeshComponent.h:52-57).
        atlasTilesPerRow    = 1;
        atlasTilesPerColumn = 1;
        chunkColumns        = 16;
        chunkRows           = 16;
        residentChunkLimit  = 256;
        prefetchMarginChunks = 1;
        samplingQuality     = 1;
        layer               = 0;
        sortingKey          = 0;
        renderDomain        = 0;
        metallic            = 0.0f;
        roughness           = 0.75f;
        ambientOcclusion    = 1.0f;
        emissiveStrength    = 0.0f;
        alphaCutoff         = 0.5f;
        invertNormalY       = false;
        castShadow          = true;
    }

    explicit TilemapComponent(const char* path)
        : TilemapComponent() {
        tilemapPath = path ? path : "";
    }

    void setTilemap(const char* path) { tilemapPath = path ? path : ""; }
    void setAtlasTexture(const char* path) { atlasTexturePath = path ? path : ""; }

    bool isValid() const { return !tilemapPath.empty(); }
    bool isWorldLit() const noexcept { return renderDomain == 1; }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
