#include "AYEntity/TilemapChunkGeometry.h"

#include "AYEntity/2DUvMath.h"
#include "AYEntity/TilemapVisibilityRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ayt::entity
{

namespace
{

uint32_t ceilDiv(uint32_t value, uint32_t divisor) noexcept
{
    return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
}

uint32_t tileIdAt(const ayt::resource::ITilemap& map,
                  uint32_t col, uint32_t row) noexcept
{
    const uint32_t cols = map.getCols();
    if (cols == 0u || col >= cols || row >= map.getRows()) {
        return map.getDefaultTileId();
    }
    const uint64_t index64 = static_cast<uint64_t>(row) * cols + col;
    if (index64 >= map.getTileIdCount()) {
        return map.getDefaultTileId();
    }
    const uint32_t index = static_cast<uint32_t>(index64);
    if (map.getPackMode() == ayt::resource::TilemapPackMode::Narrow16) {
        const uint16_t* ids = map.getTileIds16();
        return ids == nullptr ? map.getDefaultTileId()
                              : static_cast<uint32_t>(ids[index]);
    }
    const uint32_t* ids = map.getTileIds32();
    return ids == nullptr ? map.getDefaultTileId() : ids[index];
}

uint32_t clampFloorToChunk(float world, float origin, float tileSize,
                           uint32_t chunkSize, uint32_t chunkCount) noexcept
{
    if (tileSize <= 0.0f || chunkSize == 0u || chunkCount == 0u) return 0u;
    const double cell = std::floor((static_cast<double>(world) - origin)
                                   / tileSize);
    const double chunk = std::floor(cell / static_cast<double>(chunkSize));
    if (chunk <= 0.0) return 0u;
    if (chunk >= static_cast<double>(chunkCount)) return chunkCount;
    return static_cast<uint32_t>(chunk);
}

} // namespace

TilemapChunkRect visibleTilemapChunks(
    const TilemapCameraVisibility* visibility,
    float mapOriginX, float mapOriginY,
    uint32_t cols, uint32_t rows,
    float tileWidth, float tileHeight,
    uint32_t chunkCols, uint32_t chunkRows,
    uint32_t marginChunks) noexcept
{
    TilemapChunkRect result{};
    if (cols == 0u || rows == 0u || tileWidth <= 0.0f || tileHeight <= 0.0f
        || chunkCols == 0u || chunkRows == 0u) {
        return result;
    }

    const uint32_t countX = ceilDiv(cols, chunkCols);
    const uint32_t countY = ceilDiv(rows, chunkRows);
    if (visibility == nullptr || !visibility->valid) {
        result.maxChunkX = countX;
        result.maxChunkY = countY;
        return result;
    }

    const float mapMaxX = mapOriginX + static_cast<float>(cols) * tileWidth;
    const float mapMaxY = mapOriginY + static_cast<float>(rows) * tileHeight;
    if (visibility->maxX <= mapOriginX || visibility->maxY <= mapOriginY
        || visibility->minX >= mapMaxX || visibility->minY >= mapMaxY) {
        return result;
    }

    uint32_t minX = clampFloorToChunk(visibility->minX, mapOriginX,
                                      tileWidth, chunkCols, countX);
    uint32_t minY = clampFloorToChunk(visibility->minY, mapOriginY,
                                      tileHeight, chunkRows, countY);
    // max bounds are inclusive in visibility space. nextafter keeps a camera
    // exactly on a chunk boundary from pulling the following chunk by accident.
    const float maxWorldX = std::nextafter(visibility->maxX,
                                           -std::numeric_limits<float>::infinity());
    const float maxWorldY = std::nextafter(visibility->maxY,
                                           -std::numeric_limits<float>::infinity());
    uint32_t maxX = clampFloorToChunk(maxWorldX, mapOriginX,
                                      tileWidth, chunkCols, countX);
    uint32_t maxY = clampFloorToChunk(maxWorldY, mapOriginY,
                                      tileHeight, chunkRows, countY);
    if (maxX < countX) ++maxX;
    if (maxY < countY) ++maxY;

    minX = minX > marginChunks ? minX - marginChunks : 0u;
    minY = minY > marginChunks ? minY - marginChunks : 0u;
    maxX = std::min(countX, maxX + marginChunks);
    maxY = std::min(countY, maxY + marginChunks);

    result.minChunkX = minX;
    result.minChunkY = minY;
    result.maxChunkX = maxX;
    result.maxChunkY = maxY;
    return result;
}

TilemapChunkGeometry buildTilemapChunkGeometry(
    const ayt::resource::ITilemap& map,
    uint32_t beginCol, uint32_t beginRow,
    uint32_t endCol, uint32_t endRow,
    const AtlasGridDesc& atlas,
    const std::vector<uint32_t>* resolvedTileIds)
{
    TilemapChunkGeometry out;
    beginCol = std::min(beginCol, map.getCols());
    beginRow = std::min(beginRow, map.getRows());
    endCol = std::min(std::max(endCol, beginCol), map.getCols());
    endRow = std::min(std::max(endRow, beginRow), map.getRows());
    const uint64_t cells64 = static_cast<uint64_t>(endCol - beginCol)
                           * static_cast<uint64_t>(endRow - beginRow);
    // uint16 indices address four vertices per cell. Component validation caps
    // chunks below this, but keep the builder independently safe.
    if (cells64 == 0u || cells64 > 16383u) return out;

    out.cells = static_cast<uint32_t>(cells64);
    out.vertices.reserve(static_cast<size_t>(out.cells) * 4u);
    out.indices.reserve(static_cast<size_t>(out.cells) * 6u);

    const float tileW = static_cast<float>(map.getTileWidth());
    const float tileH = static_cast<float>(map.getTileHeight());

    for (uint32_t row = beginRow; row < endRow; ++row) {
        for (uint32_t col = beginCol; col < endCol; ++col) {
            uint32_t tileId = tileIdAt(map, col, row);
            if (resolvedTileIds != nullptr && tileId < resolvedTileIds->size()) {
                tileId = (*resolvedTileIds)[tileId];
            }
            const TileUvQuad uv = tileUvQuad(tileId, atlas);
            const float x0 = static_cast<float>(col) * tileW;
            const float y0 = static_cast<float>(row) * tileH;
            const float x1 = x0 + tileW;
            const float y1 = y0 + tileH;
            const uint16_t base = static_cast<uint16_t>(out.vertices.size());
            out.vertices.push_back({x0, y0, 0.0f, uv.uMin, uv.vMax});
            out.vertices.push_back({x1, y0, 0.0f, uv.uMax, uv.vMax});
            out.vertices.push_back({x1, y1, 0.0f, uv.uMax, uv.vMin});
            out.vertices.push_back({x0, y1, 0.0f, uv.uMin, uv.vMin});
            out.indices.insert(out.indices.end(), {
                base, static_cast<uint16_t>(base + 1u),
                static_cast<uint16_t>(base + 2u), base,
                static_cast<uint16_t>(base + 2u),
                static_cast<uint16_t>(base + 3u)});
        }
    }
    return out;
}

} // namespace ayt::entity
