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

    // Path fields declared via the AY_PROPERTY macro below. The
    // expansion emits `Type name;` and registers a serializer metadata
    // entry — keep these macro-only (see AYEntity/components/AYEntity/components/AYEntity/components/MeshComponent.h:6-8).
    AY_PROPERTY(std::string, tilemapPath, kAttrSerialize)
    AY_PROPERTY(std::string, atlasTexturePath, kAttrSerialize)
    // Atlas grid layout. The atlas texture is assumed to be a dense
    // grid of tiles of size (IAYTilemap::getTileWidth() x
    // getTileHeight()) with zero gutter — the AY2D dense-atlas
    // convention (ATLAS origin-bottom-left, tile-id 0 at bottom-left).
    AY_PROPERTY(int32_t, atlasTilesPerRow, kAttrSerialize)
    AY_PROPERTY(int32_t, atlasTilesPerColumn, kAttrSerialize)
    // 2D draw ordering: layer (high byte of packedSortKey) wins, then
    // sortingKey (low 24 bits). See design.md §7.4 / DrawPayload2D.
    AY_PROPERTY(int32_t, layer, kAttrSerialize)
    AY_PROPERTY(int32_t, sortingKey, kAttrSerialize)

    // Runtime-only (not serialized): render skip flag.
    bool visible = true;

    TilemapComponent() {
        // AY_PROPERTY expands to `Type name;` with no initializer —
        // explicit ctor assignment is required (AYEntity/components/AYEntity/components/AYEntity/components/MeshComponent.h:52-57).
        atlasTilesPerRow    = 1;
        atlasTilesPerColumn = 1;
        layer               = 0;
        sortingKey          = 0;
    }

    explicit TilemapComponent(const char* path)
        : tilemapPath(path ? path : "") {}

    void setTilemap(const char* path) { tilemapPath = path ? path : ""; }
    void setAtlasTexture(const char* path) { atlasTexturePath = path ? path : ""; }

    bool isValid() const { return !tilemapPath.empty(); }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
