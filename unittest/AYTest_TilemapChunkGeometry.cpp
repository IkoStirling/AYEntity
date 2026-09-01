#include <AYEntity/TilemapChunkGeometry.h>
#include <AYEntity/TilemapVisibilityRuntime.h>
#include <AYResource/assetsImpl/TilemapAsset.h>
#include <AYTest.h>

using namespace ayt::entity;
using namespace ayt::resource;

TEST_SUITE(TilemapChunkGeometryTests)

TEST_CASE(VisibilityMapsCameraToChunkRange)
{
    const TilemapChunkRect full = visibleTilemapChunks(
        nullptr, 0.0f, 0.0f, 34u, 17u, 32.0f, 32.0f,
        16u, 16u, 0u);
    CHECK_INT_EQ(full.minChunkX, 0u);
    CHECK_INT_EQ(full.minChunkY, 0u);
    CHECK_INT_EQ(full.maxChunkX, 3u);
    CHECK_INT_EQ(full.maxChunkY, 2u);

    TilemapCameraVisibility camera;
    camera.valid = true;
    camera.minX = 512.0f;
    camera.maxX = 1024.0f;
    camera.minY = 0.0f;
    camera.maxY = 512.0f;
    const TilemapChunkRect exact = visibleTilemapChunks(
        &camera, 0.0f, 0.0f, 34u, 17u, 32.0f, 32.0f,
        16u, 16u, 0u);
    CHECK_INT_EQ(exact.minChunkX, 1u);
    CHECK_INT_EQ(exact.maxChunkX, 2u);
    CHECK_INT_EQ(exact.minChunkY, 0u);
    CHECK_INT_EQ(exact.maxChunkY, 1u);

    const TilemapChunkRect prefetched = visibleTilemapChunks(
        &camera, 0.0f, 0.0f, 34u, 17u, 32.0f, 32.0f,
        16u, 16u, 1u);
    CHECK_INT_EQ(prefetched.minChunkX, 0u);
    CHECK_INT_EQ(prefetched.maxChunkX, 3u);
    CHECK_INT_EQ(prefetched.minChunkY, 0u);
    CHECK_INT_EQ(prefetched.maxChunkY, 2u);
}

TEST_CASE(VisibilityOutsideFiniteMapIsEmpty)
{
    TilemapCameraVisibility camera;
    camera.valid = true;
    camera.minX = 5000.0f;
    camera.maxX = 5100.0f;
    camera.minY = 5000.0f;
    camera.maxY = 5100.0f;
    CHECK_TRUE(visibleTilemapChunks(
        &camera, 0.0f, 0.0f, 4u, 4u, 16.0f, 16.0f,
        2u, 2u, 1u).empty());
}

TEST_CASE(ChunkGeometryBatchesCellsAndBakesAtlasUv)
{
    TilemapAsset map;
    map.create(2u, 3u, 32u, 16u,
               TilemapPackMode::Narrow16, 5u, nullptr, 0u);
    CHECK_TRUE(map.isLoaded());
    AtlasGridDesc atlas;
    atlas.tilesPerRow = 8u;
    atlas.tilesPerColumn = 4u;
    atlas.tileWidthTexels = 32u;
    atlas.tileHeightTexels = 16u;
    TilemapChunkGeometry geometry = buildTilemapChunkGeometry(
        map, 0u, 0u, 2u, 3u, atlas);
    CHECK_INT_EQ(geometry.cells, 6u);
    CHECK_INT_EQ(static_cast<uint32_t>(geometry.vertices.size()), 24u);
    CHECK_INT_EQ(static_cast<uint32_t>(geometry.indices.size()), 36u);
    CHECK_FLOAT_EQ(geometry.vertices[0].x, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[0].y, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[2].x, 32.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[2].y, 16.0f, 1e-6f);
    // Dense atlas UVs retain AY2D's half-texel inset.
    CHECK_FLOAT_EQ(geometry.vertices[0].u, 5.0f / 8.0f + 0.5f / 256.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[0].v, 1.0f / 4.0f - 0.5f / 64.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[2].u, 6.0f / 8.0f - 0.5f / 256.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[2].v, 0.5f / 64.0f, 1e-6f);
    CHECK_INT_EQ(geometry.indices[0], 0u);
    CHECK_INT_EQ(geometry.indices[5], 3u);
    CHECK_INT_EQ(geometry.indices[6], 4u);
}

TEST_CASE(FormalAtlasUsesIndependentTexelSizeAndGutter)
{
    TilemapAsset map;
    map.create(1u, 1u, 64u, 48u,
               TilemapPackMode::Narrow16, 5u, nullptr, 0u);
    AtlasGridDesc atlas;
    atlas.tilesPerRow = 8u;
    atlas.tilesPerColumn = 4u;
    atlas.tileWidthTexels = 32u;
    atlas.tileHeightTexels = 16u;
    atlas.atlasWidthTexels = 256u;
    atlas.atlasHeightTexels = 64u;
    atlas.gutter = 1u;

    const TilemapChunkGeometry geometry = buildTilemapChunkGeometry(
        map, 0u, 0u, 1u, 1u, atlas);
    CHECK_INT_EQ(geometry.cells, 1u);
    CHECK_FLOAT_EQ(geometry.vertices[0].x, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[2].x, 64.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[2].y, 48.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[0].u,
                   5.0f / 8.0f + 1.5f / 256.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[0].v,
                   1.0f / 4.0f - 1.5f / 64.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[2].u,
                   6.0f / 8.0f - 1.5f / 256.0f, 1e-6f);
    CHECK_FLOAT_EQ(geometry.vertices[2].v,
                   1.5f / 64.0f, 1e-6f);
}

TEST_SUITE_END
