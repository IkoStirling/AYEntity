#pragma once
// AY2DUvMath.h — CM-3 (2026-08-11): AYEntity-side mirror of the AY2D
// tile->UV / tile->world math.
//
// Dependency-direction lock: AYEntity must not depend on AY2D (AY2D
// stays a pure AYMath+AYLog leaf), so the two helpers the 2D render
// systems need are duplicated here with a mirror-of comment. The
// unittest (AYTest_2DComponents.cpp) cross-asserts every value against
// the real AY2D headers (header-only, no link needed) so drift breaks
// the build.

#include <aymath/MathTypes.h>

#include <cstdint>

namespace ayt::entity
{

// Dense-atlas grid descriptor. atlasWidthTexels is implied:
// tileWidthTexels * tilesPerRow (dense packing, gutter = 0) — mirror
// of the AY2D dense-atlas convention (AYTileSamplerUV.h).
struct AtlasGridDesc {
    uint32_t tilesPerRow     = 0;
    uint32_t tilesPerColumn  = 0;
    uint32_t tileWidthTexels = 0;
    uint32_t tileHeightTexels = 0;
};

struct TileUvQuad {
    float uMin = 0.0f;
    float uMax = 0.0f;
    float vMin = 0.0f;
    float vMax = 0.0f;
};

// Mirror of ayt::ay2d::tileUV (AYTileSamplerUV.h:56-87) with gutter=0
// (dense atlas) and the half-texel center convention. UV space is
// atlas-normalized 0..1, origin-bottom-left (tile-id 0 sits at the
// bottom-left; row 0 = bottom). Out-of-range tile ids / degenerate
// descs yield all-zeros.
inline TileUvQuad tileUvQuad(uint32_t tileId, const AtlasGridDesc& d) noexcept
{
    TileUvQuad uv{};
    if (d.tilesPerRow == 0 || d.tilesPerColumn == 0) return uv;
    if (d.tileWidthTexels == 0 || d.tileHeightTexels == 0) return uv;

    const uint32_t atlasW = d.tileWidthTexels * d.tilesPerRow;
    const uint32_t atlasH = d.tileHeightTexels * d.tilesPerColumn;
    if (atlasW == 0 || atlasH == 0) return uv;

    const uint32_t col = tileId % d.tilesPerRow;
    const uint32_t row = tileId / d.tilesPerRow;

    const float aw = static_cast<float>(atlasW);
    const float ah = static_cast<float>(atlasH);
    const float half_px = 0.5f / aw;  // half-texel in U (design.md §5.1)
    const float half_py = 0.5f / ah;  // half-texel in V

    const float tileLeft   = static_cast<float>(col)      * static_cast<float>(d.tileWidthTexels)  / aw;
    const float tileRight  = static_cast<float>(col + 1u) * static_cast<float>(d.tileWidthTexels)  / aw;
    const float tileBottom = static_cast<float>(row)      * static_cast<float>(d.tileHeightTexels) / ah;
    const float tileTop    = static_cast<float>(row + 1u) * static_cast<float>(d.tileHeightTexels) / ah;

    uv.uMin = tileLeft   + half_px;
    uv.uMax = tileRight  - half_px;
    uv.vMin = tileBottom + half_py;
    uv.vMax = tileTop    - half_py;
    return uv;
}

// Mirror of ayt::ay2d::cellToWorld (AYTileMath.h:65-81) with
// cellOrigin = {0,0} (the Tilemap::setTile world-coord convention):
// the world-space CENTER of cell (col, row), in world units.
// Degenerate cell sizes return (0,0).
inline ayt::math::FVector2 cellCenterWorld(uint32_t col, uint32_t row,
                                           float cellSizeW, float cellSizeH) noexcept
{
    if (cellSizeW <= 0.0f || cellSizeH <= 0.0f) return ayt::math::FVector2{0.0f, 0.0f};
    return ayt::math::FVector2{
        (static_cast<float>(col) + 0.5f) * cellSizeW,
        (static_cast<float>(row) + 0.5f) * cellSizeH,
    };
}

// 2D draw ordering (design.md §7.4 / DrawPayload2D): layer in the
// high byte wins, then sortingKey in the low 24 bits. Ascending order
// = front-to-back of the sort key; the Forward2DOpaquePass stable
// sorts the same way, so equal keys keep author order.
inline uint32_t drawSortKey(int32_t layer, int32_t sortingKey) noexcept
{
    const uint32_t l = static_cast<uint32_t>(layer) & 0xFFu;
    const uint32_t k = static_cast<uint32_t>(sortingKey) & 0xFFFFFFu;
    return (l << 24) | k;
}

} // namespace ayt::entity
