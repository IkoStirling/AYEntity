#include "AYEntity/TilemapChunkGeometry.h"

#include "AYEntity/2DUvMath.h"
#include "AYEntity/TilemapVisibilityRuntime.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <unordered_map>

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

uint32_t tileIdAtLayer(const ayt::resource::ITilemap& map,
                       uint32_t layerIndex,
                       uint32_t col, uint32_t row) noexcept
{
    const uint32_t cols = map.getCols();
    if (cols == 0u || col >= cols || row >= map.getRows()) {
        return map.getDefaultTileId();
    }
    const uint64_t index64 = static_cast<uint64_t>(row) * cols + col;
    if (index64 >= map.getLayerTileIdCount(layerIndex)) {
        return map.getDefaultTileId();
    }
    const uint32_t index = static_cast<uint32_t>(index64);
    if (map.getPackMode() == ayt::resource::TilemapPackMode::Narrow16) {
        const uint16_t* ids = map.getLayerTileIds16(layerIndex);
        return ids == nullptr ? map.getDefaultTileId()
                              : static_cast<uint32_t>(ids[index]);
    }
    const uint32_t* ids = map.getLayerTileIds32(layerIndex);
    return ids == nullptr ? map.getDefaultTileId() : ids[index];
}

void appendQuad(TilemapChunkGeometry& out,
                float x0, float y0, float x1, float y1,
                float u0, float v0, float u1, float v1)
{
    if (out.vertices.size() > 65531u) return;
    const uint16_t base = static_cast<uint16_t>(out.vertices.size());
    out.vertices.push_back({x0, y0, 0.0f, u0, v1});
    out.vertices.push_back({x1, y0, 0.0f, u1, v1});
    out.vertices.push_back({x1, y1, 0.0f, u1, v0});
    out.vertices.push_back({x0, y1, 0.0f, u0, v0});
    out.indices.insert(out.indices.end(), {
        base, static_cast<uint16_t>(base + 1u),
        static_cast<uint16_t>(base + 2u), base,
        static_cast<uint16_t>(base + 2u),
        static_cast<uint16_t>(base + 3u)});
    ++out.cells;
}

bool finiteMatrix(const ayt::math::Float4x4& matrix) noexcept
{
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (!std::isfinite(matrix.row[row][column])) {
                return false;
            }
        }
    }
    return true;
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

bool tilemapChunkIntersectsFrustum(
    const ayt::math::Float4x4& view,
    const ayt::math::Float4x4& projection,
    const ayt::math::Float4x4& world,
    float localMinX, float localMinY,
    float localMaxX, float localMaxY) noexcept
{
    if (!finiteMatrix(view) || !finiteMatrix(projection)
        || !finiteMatrix(world)
        || !std::isfinite(localMinX) || !std::isfinite(localMinY)
        || !std::isfinite(localMaxX) || !std::isfinite(localMaxY)
        || localMinX > localMaxX || localMinY > localMaxY) {
        return true;
    }

    const ayt::math::Float4x4 mvp = projection * view * world;
    const ayt::math::FVector4 clip[] = {
        mvp * ayt::math::FVector4(localMinX, localMinY, 0.0f, 1.0f),
        mvp * ayt::math::FVector4(localMaxX, localMinY, 0.0f, 1.0f),
        mvp * ayt::math::FVector4(localMaxX, localMaxY, 0.0f, 1.0f),
        mvp * ayt::math::FVector4(localMinX, localMaxY, 0.0f, 1.0f),
    };
    for (const ayt::math::FVector4& point : clip) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)
            || !std::isfinite(point.z) || !std::isfinite(point.w)) {
            return true;
        }
    }

    const auto allOutside = [&clip](const auto& predicate) noexcept {
        return std::all_of(std::begin(clip), std::end(clip), predicate);
    };
    return !(allOutside([](const auto& p) { return p.x < -p.w; })
          || allOutside([](const auto& p) { return p.x >  p.w; })
          || allOutside([](const auto& p) { return p.y < -p.w; })
          || allOutside([](const auto& p) { return p.y >  p.w; })
          || allOutside([](const auto& p) { return p.z < 0.0f; })
          || allOutside([](const auto& p) { return p.z > p.w; }));
}

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

std::vector<TilemapChunkBatch> buildTilemapChunkBatches(
    const ayt::resource::ITilemap& map, uint32_t layerIndex,
    uint32_t beginCol, uint32_t beginRow,
    uint32_t endCol, uint32_t endRow,
    const std::vector<uint32_t>* resolvedTileIds)
{
    std::vector<TilemapChunkBatch> out;
    if (layerIndex >= map.getLayerCount() || !map.isLayerVisible(layerIndex)
        || map.getVisualCount() == 0u) {
        return out;
    }
    beginCol = std::min(beginCol, map.getCols());
    beginRow = std::min(beginRow, map.getRows());
    endCol = std::min(std::max(endCol, beginCol), map.getCols());
    endRow = std::min(std::max(endRow, beginRow), map.getRows());

    std::unordered_map<uint32_t, const ayt::resource::TilemapVisualEntry*> visuals;
    const auto* entries = map.getVisualEntries();
    for (uint32_t i = 0u; i < map.getVisualCount(); ++i) {
        visuals.emplace(entries[i].tileId, entries + i);
    }
    std::unordered_map<uint32_t, const ayt::resource::TilemapAtlasEntry*> atlases;
    const auto* atlasEntries = map.getAtlasEntries();
    for (uint32_t i = 0u; i < map.getAtlasCount(); ++i) {
        atlases.emplace(atlasEntries[i].atlasId, atlasEntries + i);
    }
    std::unordered_map<uint64_t, size_t> batchIndices;
    const float tileW = static_cast<float>(map.getTileWidth());
    const float tileH = static_cast<float>(map.getTileHeight());
    for (uint32_t row = beginRow; row < endRow; ++row) {
        for (uint32_t col = beginCol; col < endCol; ++col) {
            uint32_t tileId = tileIdAtLayer(map, layerIndex, col, row);
            if (resolvedTileIds != nullptr && tileId < resolvedTileIds->size()) {
                tileId = (*resolvedTileIds)[tileId];
            }
            const auto visualIt = visuals.find(tileId);
            if (visualIt == visuals.end()) continue;
            const auto& visual = *visualIt->second;
            const auto atlasIt = atlases.find(visual.atlasId);
            if (atlasIt == atlases.end()) continue;
            const auto& atlas = *atlasIt->second;
            const uint64_t key = (static_cast<uint64_t>(visual.atlasId) << 32u)
                               | visual.tintRgba;
            auto [batchIt, inserted] = batchIndices.emplace(key, out.size());
            if (inserted) {
                TilemapChunkBatch batch;
                batch.atlasId = visual.atlasId;
                batch.tintRgba = visual.tintRgba;
                batch.atlasWidth = atlas.imageWidth;
                batch.atlasHeight = atlas.imageHeight;
                out.push_back(std::move(batch));
            }
            TilemapChunkGeometry& geometry = out[batchIt->second].geometry;
            const float invW = 1.0f / static_cast<float>(atlas.imageWidth);
            const float invH = 1.0f / static_cast<float>(atlas.imageHeight);
            const float u0 = (static_cast<float>(visual.sourceX) + 0.5f) * invW;
            const float v0 = (static_cast<float>(visual.sourceY) + 0.5f) * invH;
            const float u1 = (static_cast<float>(visual.sourceX + visual.sourceWidth)
                              - 0.5f) * invW;
            const float v1 = (static_cast<float>(visual.sourceY + visual.sourceHeight)
                              - 0.5f) * invH;
            const float x0 = static_cast<float>(col) * tileW;
            const float y0 = static_cast<float>(row) * tileH;
            appendQuad(geometry, x0, y0, x0 + tileW, y0 + tileH,
                       u0, v0, u1, v1);
        }
    }
    return out;
}

TilemapChunkGeometry buildTilemapShadowGeometry(
    const ayt::resource::ITilemap& map,
    uint32_t beginCol, uint32_t beginRow,
    uint32_t endCol, uint32_t endRow)
{
    TilemapChunkGeometry out;
    const uint8_t* masks = map.getShadowMasks();
    if (masks == nullptr || map.getShadowMaskCount() == 0u) return out;
    beginCol = std::min(beginCol, map.getCols());
    beginRow = std::min(beginRow, map.getRows());
    endCol = std::min(std::max(endCol, beginCol), map.getCols());
    endRow = std::min(std::max(endRow, beginRow), map.getRows());
    const float tileW = static_cast<float>(map.getTileWidth());
    const float tileH = static_cast<float>(map.getTileHeight());
    for (uint32_t row = beginRow; row < endRow; ++row) {
        for (uint32_t col = beginCol; col < endCol; ++col) {
            const uint64_t index = static_cast<uint64_t>(row) * map.getCols() + col;
            if (index >= map.getShadowMaskCount()) continue;
            const uint8_t mask = static_cast<uint8_t>(masks[index] & 0x0fu);
            const float x0 = static_cast<float>(col) * tileW;
            const float y0 = static_cast<float>(row) * tileH;
            const float xm = x0 + tileW * 0.5f;
            const float ym = y0 + tileH * 0.5f;
            const float x1 = x0 + tileW;
            const float y1 = y0 + tileH;
            // World Y grows upward, so authored top quadrants use y=[mid,max].
            if ((mask & 0x01u) != 0u) appendQuad(out, x0, ym, xm, y1, 0, 0, 1, 1);
            if ((mask & 0x02u) != 0u) appendQuad(out, xm, ym, x1, y1, 0, 0, 1, 1);
            if ((mask & 0x04u) != 0u) appendQuad(out, x0, y0, xm, ym, 0, 0, 1, 1);
            if ((mask & 0x08u) != 0u) appendQuad(out, xm, y0, x1, ym, 0, 0, 1, 1);
        }
    }
    return out;
}

} // namespace ayt::entity
