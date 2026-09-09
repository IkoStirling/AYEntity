#pragma once

#include "AYEntity/2DUvMath.h"

#include <AYResource/assetsDefs/ITilemap.h>

#include <cstdint>
#include <vector>

namespace ayt::entity
{

struct TilemapCameraVisibility;

struct TilemapChunkRect {
    uint32_t minChunkX = 0;
    uint32_t minChunkY = 0;
    uint32_t maxChunkX = 0; // exclusive
    uint32_t maxChunkY = 0; // exclusive

    [[nodiscard]] bool empty() const noexcept {
        return minChunkX >= maxChunkX || minChunkY >= maxChunkY;
    }
};

struct TilemapChunkVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

struct TilemapChunkGeometry {
    std::vector<TilemapChunkVertex> vertices;
    std::vector<uint16_t> indices;
    uint32_t cells = 0;
};

// Conservative homogeneous-frustum test for one flat tilemap chunk. Camera
// depth follows the engine LH [0,1] convention. Invalid/non-finite inputs fail
// open so malformed host camera data cannot make world geometry disappear.
[[nodiscard]] bool tilemapChunkIntersectsFrustum(
    const ayt::math::Float4x4& view,
    const ayt::math::Float4x4& projection,
    const ayt::math::Float4x4& world,
    float localMinX, float localMinY,
    float localMaxX, float localMaxY) noexcept;

// Convert a camera world rectangle to a clamped chunk range. `visibility ==
// nullptr` or !valid returns the full finite map. max values are exclusive.
[[nodiscard]] TilemapChunkRect visibleTilemapChunks(
    const TilemapCameraVisibility* visibility,
    float mapOriginX, float mapOriginY,
    uint32_t cols, uint32_t rows,
    float tileWidth, float tileHeight,
    uint32_t chunkCols, uint32_t chunkRows,
    uint32_t marginChunks) noexcept;

// Builds one indexed quad mesh for a half-open cell chunk. Atlas V is baked in
// renderer sampling orientation (bottom vertices use vMax), so the chunk shader
// samples uvOut directly. `resolvedTileIds` is an optional animation snapshot.
[[nodiscard]] TilemapChunkGeometry buildTilemapChunkGeometry(
    const ayt::resource::ITilemap& map,
    uint32_t beginCol, uint32_t beginRow,
    uint32_t endCol, uint32_t endRow,
    const AtlasGridDesc& atlas,
    const std::vector<uint32_t>* resolvedTileIds = nullptr);

} // namespace ayt::entity
