#include "AYEntity/TilemapRenderSystem.h"

#include "AYEntity/2DUvMath.h"
#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
#include "AYEntity/TilemapAnimationRuntime.h"
#include "AYEntity/TilemapChunkGeometry.h"
#include "AYEntity/TilemapVisibilityRuntime.h"
#include "AYEntity/World.h"
#include "AYEntity/components/TilemapComponent.h"
#include "AYEntity/components/TransformComponent.h"
#include "AYRenderer/RendererSubSystem.h"
#include "AYRenderer/TilemapShaderSources.h"

#include <AYMath/MathTransform.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/assetsDefs/ITilemap.h>
#include <AYResource/assetsDefs/IAtlas.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ayt::entity
{

namespace
{

uint32_t clampChunkDimension(int32_t value) noexcept
{
    if (value <= 0) return 16u;
    return std::min(64u, static_cast<uint32_t>(value));
}

uint32_t clampSamplingQuality(int32_t value) noexcept
{
    return value >= 0 && value <= 3 ? static_cast<uint32_t>(value) : 1u;
}

uint64_t packChunkKey(uint32_t x, uint32_t y) noexcept
{
    return (static_cast<uint64_t>(x) << 32u) | y;
}

bool isAxisAlignedPositive(const Transform& transform) noexcept
{
    constexpr float epsilon = 1.0e-5f;
    return std::fabs(transform.rotation.x) <= epsilon
        && std::fabs(transform.rotation.y) <= epsilon
        && std::fabs(transform.rotation.z) <= epsilon
        && std::fabs(transform.rotation.w - 1.0f) <= epsilon
        && transform.scale.x > 0.0f && transform.scale.y > 0.0f;
}

const char* samplingSource(uint32_t quality) noexcept
{
    return ayt::render::tilemapChunkShaderSource(
        static_cast<ayt::render::TilemapSamplingQuality>(quality));
}

std::string cacheKey(const TilemapComponent& component,
                     uint32_t chunkCols, uint32_t chunkRows)
{
    return component.tilemapPath + "|" + component.atlasPath + "|"
         + component.atlasTexturePath + "|"
         + std::to_string(component.atlasTilesPerRow) + "x"
         + std::to_string(component.atlasTilesPerColumn) + "|"
         + std::to_string(chunkCols) + "x" + std::to_string(chunkRows);
}

} // namespace

void TilemapRenderSystem::onStart()
{
    // Visibility belongs to the active World frame. Drop a stale snapshot
    // left by a previously shut-down/test World; StreamingSystem republishes
    // before render on every production frame (priority 430 < 510).
    TilemapVisibilityRuntime::instance().clear();
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        std::fprintf(stderr,
                     "[TilemapRenderSystem] RendererSubSystem not registered; "
                     "tilemap draws will not be submitted.\n");
        return;
    }
    World* owner = &World::instance();
    rss->addSceneBuilderForOwner(
        owner, [this, owner](ayt::render::RenderScene& scene) {
            if (&World::instance() == owner) buildRenderScene(scene);
        });
    _started = true;
}

void TilemapRenderSystem::buildRenderScene(ayt::render::RenderScene& scene)
{
    ++_frameIndex;
    _lastStats = {};
    _payloads.clear();

    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) return;
    ayt::render::Renderer& renderer = rss->renderer();

    struct ChunkDraw {
        ayt::render::DrawItem item;
        ayt::render::DrawPayload2D payload;
    };
    std::vector<ChunkDraw> draws;

    const TilemapCameraVisibility& visibility =
        TilemapVisibilityRuntime::instance().current();
    World& world = World::instance();
    for (Entity* entity : world.query<Transform, TilemapComponent>()) {
        if (entity == nullptr) continue;
        Transform* transform = entity->getComponent<Transform>();
        TilemapComponent* component = entity->getComponent<TilemapComponent>();
        if (transform == nullptr || component == nullptr || !component->visible
            || !component->isValid()) {
            continue;
        }
        const uint32_t layer = static_cast<uint32_t>(component->layer) & 0x1Fu;
        if (visibility.valid && (visibility.layerMask & (1u << layer)) == 0u) {
            continue;
        }

        const uint32_t chunkCols = clampChunkDimension(component->chunkColumns);
        const uint32_t chunkRows = clampChunkDimension(component->chunkRows);
        CachedTilemapResources& resources =
            _cache[cacheKey(*component, chunkCols, chunkRows)];
        if (!resources.tilemap) {
            resources.tilemap = ayt::resource::ResourceManager::instance()
                .load<ayt::resource::ITilemap>(component->tilemapPath);
            if (!resources.tilemap && _frameIndex <= 5u) {
                std::fprintf(stderr,
                             "[TilemapRenderSystem] load<ITilemap> failed: '%s'\n",
                             component->tilemapPath.c_str());
            }
        }
        if (!resources.tilemap) continue;

        if (!component->atlasPath.empty() && !resources.atlas) {
            resources.atlas = ayt::resource::ResourceManager::instance()
                .load<ayt::resource::IAtlas>(component->atlasPath);
        }
        const std::string texturePath = resources.atlas
            ? resources.atlas->getTexturePath() : component->atlasTexturePath;
        if (!resources.texture.isValid() && !texturePath.empty()) {
            resources.texture = renderer.loadTexture(texturePath);
        }
        const uint32_t quality = resources.atlas
            ? static_cast<uint32_t>(resources.atlas->getFilter())
            : clampSamplingQuality(component->samplingQuality);
        ayt::render::MaterialHandle& material = resources.materials[quality];
        if (!material.isValid() && resources.texture.isValid()) {
            material = renderer.createMaterialFromPhoskia(
                samplingSource(quality),
                texturePath + "#tilemap_chunk_q" + std::to_string(quality));
            if (material.isValid()) {
                renderer.setMaterialTexture(material, "albedoMap", resources.texture);
            }
        }
        if (!resources.texture.isValid() || !material.isValid()) continue;

        const uint32_t cols = resources.tilemap->getCols();
        const uint32_t rows = resources.tilemap->getRows();
        const float tileW = static_cast<float>(resources.tilemap->getTileWidth());
        const float tileH = static_cast<float>(resources.tilemap->getTileHeight());
        if (cols == 0u || rows == 0u || tileW <= 0.0f || tileH <= 0.0f) continue;

        const TilemapCameraVisibility* cull =
            isAxisAlignedPositive(*transform) ? &visibility : nullptr;
        const uint32_t margin = component->prefetchMarginChunks > 0
            ? static_cast<uint32_t>(component->prefetchMarginChunks) : 0u;
        const TilemapChunkRect range = visibleTilemapChunks(
            cull, transform->position.x, transform->position.y,
            cols, rows, tileW * transform->scale.x,
            tileH * transform->scale.y, chunkCols, chunkRows, margin);
        if (range.empty()) continue;

        TilemapAnimationRuntimeEntry* animation =
            TilemapAnimationRuntime::instance().find(component->tilemapPath);
        const uint64_t animationRevision = animation == nullptr
            ? 0u : animation->revision;
        const std::vector<uint32_t>* resolved = animation == nullptr
            ? nullptr : &animation->resolved;
        const uint32_t atlasCols = resources.atlas
            ? resources.atlas->getTilesPerRow()
            : (component->atlasTilesPerRow > 0
                ? static_cast<uint32_t>(component->atlasTilesPerRow) : 1u);
        const uint32_t atlasRows = resources.atlas
            ? resources.atlas->getTilesPerColumn()
            : (component->atlasTilesPerColumn > 0
                ? static_cast<uint32_t>(component->atlasTilesPerColumn) : 1u);
        AtlasGridDesc atlasGrid;
        atlasGrid.tilesPerRow = atlasCols;
        atlasGrid.tilesPerColumn = atlasRows;
        atlasGrid.tileWidthTexels = resources.atlas
            ? resources.atlas->getTileWidth()
            : resources.tilemap->getTileWidth();
        atlasGrid.tileHeightTexels = resources.atlas
            ? resources.atlas->getTileHeight()
            : resources.tilemap->getTileHeight();
        atlasGrid.atlasWidthTexels = resources.atlas
            ? resources.atlas->getAtlasWidth()
            : atlasGrid.tileWidthTexels * atlasCols;
        atlasGrid.atlasHeightTexels = resources.atlas
            ? resources.atlas->getAtlasHeight()
            : atlasGrid.tileHeightTexels * atlasRows;
        atlasGrid.gutter = resources.atlas ? resources.atlas->getGutter() : 0u;
        const float atlasWidth = static_cast<float>(atlasGrid.atlasWidthTexels);
        const float atlasHeight = static_cast<float>(atlasGrid.atlasHeightTexels);

        for (uint32_t chunkY = range.minChunkY;
             chunkY < range.maxChunkY; ++chunkY) {
            for (uint32_t chunkX = range.minChunkX;
                 chunkX < range.maxChunkX; ++chunkX) {
                ++_lastStats.visibleChunks;
                const uint64_t key = packChunkKey(chunkX, chunkY);
                CachedChunkMesh& cached = resources.chunks[key];
                if (!cached.mesh.isValid()
                    || cached.animationRevision != animationRevision) {
                    if (cached.mesh.isValid()) renderer.destroyMesh(cached.mesh);
                    const uint32_t beginCol = chunkX * chunkCols;
                    const uint32_t beginRow = chunkY * chunkRows;
                    const uint32_t endCol = std::min(cols, beginCol + chunkCols);
                    const uint32_t endRow = std::min(rows, beginRow + chunkRows);
                    TilemapChunkGeometry geometry = buildTilemapChunkGeometry(
                        *resources.tilemap, beginCol, beginRow, endCol, endRow,
                        atlasGrid, resolved);
                    _lastStats.cellsVisited += geometry.cells;
                    if (!geometry.vertices.empty() && !geometry.indices.empty()) {
                        cached.mesh = renderer.createMesh(
                            geometry.vertices.data(),
                            static_cast<uint32_t>(geometry.vertices.size()),
                            ayt::render::VertexLayoutDesc::position3TexCoord2(),
                            geometry.indices.data(),
                            static_cast<uint32_t>(geometry.indices.size()));
                    }
                    cached.animationRevision = animationRevision;
                    ++_lastStats.chunkMeshesBuilt;
                }
                cached.lastUsedFrame = _frameIndex;
                if (!cached.mesh.isValid()) continue;

                ChunkDraw draw;
                draw.payload.sourceRectMin = ayt::math::FVector2(0.0f, 0.0f);
                draw.payload.sourceRectMax = ayt::math::FVector2(1.0f, 1.0f);
                draw.payload.tintRGBA = ayt::math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
                draw.payload.atlasTexelSize = ayt::math::FVector2(
                    atlasWidth > 0.0f ? 1.0f / atlasWidth : 0.0f,
                    atlasHeight > 0.0f ? 1.0f / atlasHeight : 0.0f);
                draw.payload.packedSortKey =
                    drawSortKey(component->layer, component->sortingKey);
                draw.item.mesh = cached.mesh;
                draw.item.material = material;
                draw.item.world = ayt::math::Transform::getMatrix(
                    transform->position, transform->rotation, transform->scale);
                draws.push_back(draw);
            }
        }

        const uint32_t limit = component->residentChunkLimit > 0
            ? static_cast<uint32_t>(component->residentChunkLimit) : 256u;
        while (resources.chunks.size() > limit) {
            auto victim = resources.chunks.end();
            for (auto it = resources.chunks.begin(); it != resources.chunks.end(); ++it) {
                if (it->second.lastUsedFrame == _frameIndex) continue;
                if (victim == resources.chunks.end()
                    || it->second.lastUsedFrame < victim->second.lastUsedFrame) {
                    victim = it;
                }
            }
            if (victim == resources.chunks.end()) break;
            renderer.destroyMesh(victim->second.mesh);
            resources.chunks.erase(victim);
            ++_lastStats.chunkMeshesEvicted;
        }
    }

    std::stable_sort(draws.begin(), draws.end(),
        [](const ChunkDraw& a, const ChunkDraw& b) {
            return a.payload.packedSortKey < b.payload.packedSortKey;
        });
    _payloads.reserve(draws.size());
    for (ChunkDraw& draw : draws) {
        _payloads.push_back(draw.payload);
        draw.item.payload = &_payloads.back();
        scene.add(draw.item);
    }
    _lastStats.drawItemsEmitted = static_cast<uint32_t>(draws.size());
    for (const auto& pair : _cache) {
        _lastStats.residentChunkMeshes +=
            static_cast<uint32_t>(pair.second.chunks.size());
    }
}

void registerTilemapRenderSystem()
{
    World::instance().registerSystem<TilemapRenderSystem>(
        TilemapRenderSystem::kPriority);
}

} // namespace ayt::entity
