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

ayt::render::RenderDomain2D renderDomainOf(
    const TilemapComponent& component) noexcept
{
    return component.isWorldLit() ? ayt::render::RenderDomain2D::WorldLit
                                  : ayt::render::RenderDomain2D::SceneOverlay;
}

std::string worldLitMaterialKey(const TilemapComponent& component,
                                uint32_t entityId,
                                const std::string& albedoPath)
{
    std::string key = "tilemap-world-lit\x1f";
    const auto append = [&key](const std::string& value) {
        key += value;
        key.push_back('\x1f');
    };
    append(albedoPath);
    append(component.normalTexturePath);
    append(component.roughnessTexturePath);
    append(component.emissiveTexturePath);
    // Surface scalars are mutable Inspector state, so one material is owned
    // per entity/map set and updated in place rather than cached per value.
    append(std::to_string(entityId));
    return key;
}

float finiteClamped(float value, float fallback, float minimum,
                    float maximum) noexcept
{
    return std::clamp(std::isfinite(value) ? value : fallback,
                      minimum, maximum);
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
    ayt::math::Float4x4 mainCameraView;
    ayt::math::Float4x4 mainCameraProjection;
    const bool haveMainCamera = renderer.mainCameraMatrices(
        mainCameraView, mainCameraProjection);

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
        const ayt::render::RenderDomain2D renderDomain =
            renderDomainOf(*component);
        const bool worldLit =
            renderDomain == ayt::render::RenderDomain2D::WorldLit;
        const ayt::math::Float4x4 worldMatrix =
            ayt::math::Transform::getMatrix(
                transform->position, transform->rotation, transform->scale);
        const uint32_t layer = static_cast<uint32_t>(component->layer) & 0x1Fu;
        if (!worldLit && visibility.valid
            && (visibility.layerMask & (1u << layer)) == 0u) {
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
        const uint32_t quality = resources.atlas
            ? clampSamplingQuality(
                static_cast<int32_t>(resources.atlas->getFilter()))
            : clampSamplingQuality(component->samplingQuality);

        ayt::render::MaterialHandle material;
        if (worldLit) {
            const std::string surfaceKey = worldLitMaterialKey(
                *component, entity->getId(), texturePath);
            CachedWorldLitMaterial& surface = _worldLitMaterials[surfaceKey];
            if (!surface.albedo.isValid() && !texturePath.empty()) {
                surface.albedo = renderer.loadTexture(texturePath, /*srgb=*/true);
            }
            const auto loadOptionalMap = [&](const std::string& path,
                                             ayt::render::TextureHandle& texture,
                                             bool srgb) {
                if (path.empty() || texture.isValid()) {
                    return true;
                }
                texture = renderer.loadTexture(path, srgb);
                if (!texture.isValid() && _frameIndex <= 5u) {
                    std::fprintf(stderr,
                                 "[TilemapRenderSystem] optional map load "
                                 "failed: '%s'\n", path.c_str());
                }
                return texture.isValid();
            };
            const bool optionalMapsReady =
                loadOptionalMap(component->normalTexturePath,
                                surface.normal, /*srgb=*/false)
                && loadOptionalMap(component->roughnessTexturePath,
                                   surface.roughness, /*srgb=*/false)
                && loadOptionalMap(component->emissiveTexturePath,
                                   surface.emissive, /*srgb=*/true);
            if (!surface.albedo.isValid() || !optionalMapsReady) {
                continue;
            }
            if (!surface.material.isValid()) {
                ayt::render::Material2DDesc desc;
                desc.albedo = surface.albedo;
                desc.normal = surface.normal;
                desc.roughnessMap = surface.roughness;
                desc.emissiveMap = surface.emissive;
                desc.metallic = component->metallic;
                desc.roughness = component->roughness;
                desc.ambientOcclusion = component->ambientOcclusion;
                desc.emissiveStrength = component->emissiveStrength;
                desc.alphaCutoff = component->alphaCutoff;
                desc.alphaMode = ayt::render::Material2DAlphaMode::Cutout;
                desc.invertNormalY = component->invertNormalY;
                desc.doubleSided = true;
                surface.material = renderer.createMaterial2D(
                    desc, surfaceKey + "material");
            }
            if (!surface.material.isValid()) {
                continue;
            }
            renderer.setMaterialFloat(
                surface.material, "metallic",
                finiteClamped(component->metallic, 0.0f, 0.0f, 1.0f));
            renderer.setMaterialFloat(
                surface.material, "roughness",
                finiteClamped(component->roughness, 0.75f, 0.045f, 1.0f));
            renderer.setMaterialFloat(
                surface.material, "ao",
                finiteClamped(component->ambientOcclusion, 1.0f, 0.0f, 1.0f));
            renderer.setMaterialFloat(
                surface.material, "normalYSign",
                component->invertNormalY ? -1.0f : 1.0f);
            const float emissive = finiteClamped(
                component->emissiveStrength, 0.0f, 0.0f, 64.0f);
            renderer.setMaterialVec3(surface.material, "emissive",
                                     emissive, emissive, emissive);
            renderer.setMaterialSurfaceProperties(
                surface.material,
                static_cast<int>(ayt::render::Material2DAlphaMode::Cutout),
                finiteClamped(component->alphaCutoff, 0.5f, 0.0f, 1.0f),
                /*doubleSided=*/true);
            material = surface.material;
        } else {
            if (!resources.texture.isValid() && !texturePath.empty()) {
                resources.texture = renderer.loadTexture(texturePath);
            }
            ayt::render::MaterialHandle& overlayMaterial =
                resources.materials[quality];
            if (!overlayMaterial.isValid() && resources.texture.isValid()) {
                overlayMaterial = renderer.createMaterialFromPhoskia(
                    samplingSource(quality),
                    texturePath + "#tilemap_chunk_q" + std::to_string(quality));
                if (overlayMaterial.isValid()) {
                    renderer.setMaterialTexture(
                        overlayMaterial, "albedoMap", resources.texture);
                }
            }
            if (!resources.texture.isValid() || !overlayMaterial.isValid()) {
                continue;
            }
            material = overlayMaterial;
        }

        const uint32_t cols = resources.tilemap->getCols();
        const uint32_t rows = resources.tilemap->getRows();
        const float tileW = static_cast<float>(resources.tilemap->getTileWidth());
        const float tileH = static_cast<float>(resources.tilemap->getTileHeight());
        if (cols == 0u || rows == 0u || tileW <= 0.0f || tileH <= 0.0f) continue;

        // Overlay uses its independent orthographic camera rectangle. WorldLit
        // starts from the finite map and rejects individual chunks against the
        // current perspective camera below; no authored main camera fails open.
        const TilemapCameraVisibility* cull =
            !worldLit && isAxisAlignedPositive(*transform)
                ? &visibility : nullptr;
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
                const uint32_t beginCol = chunkX * chunkCols;
                const uint32_t beginRow = chunkY * chunkRows;
                const uint32_t endCol = std::min(cols, beginCol + chunkCols);
                const uint32_t endRow = std::min(rows, beginRow + chunkRows);
                if (worldLit && haveMainCamera) {
                    const float marginX = static_cast<float>(margin)
                                        * static_cast<float>(chunkCols) * tileW;
                    const float marginY = static_cast<float>(margin)
                                        * static_cast<float>(chunkRows) * tileH;
                    if (!tilemapChunkIntersectsFrustum(
                            mainCameraView, mainCameraProjection, worldMatrix,
                            static_cast<float>(beginCol) * tileW - marginX,
                            static_cast<float>(beginRow) * tileH - marginY,
                            static_cast<float>(endCol) * tileW + marginX,
                            static_cast<float>(endRow) * tileH + marginY)) {
                        continue;
                    }
                }

                ++_lastStats.visibleChunks;
                const uint64_t key = packChunkKey(chunkX, chunkY);
                CachedChunkMesh& cached = resources.chunks[key];
                if (!cached.mesh.isValid()
                    || cached.animationRevision != animationRevision) {
                    if (cached.mesh.isValid()) renderer.destroyMesh(cached.mesh);
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
                draw.payload.renderDomain = renderDomain;
                draw.payload.uvMapping = ayt::render::UvMapping2D::BakedAtlas;
                draw.payload.samplingQuality =
                    static_cast<ayt::render::TilemapSamplingQuality>(quality);
                draw.item.mesh = cached.mesh;
                draw.item.material = material;
                draw.item.world = worldMatrix;
                draw.item.shadowFlags = worldLit
                    ? ayt::render::makeShadowFlags(
                        component->castShadow, /*receive=*/true)
                    : ayt::render::ShadowFlags::None;
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
