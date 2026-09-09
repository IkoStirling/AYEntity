// AYSpriteRenderSystem.cpp — CM-3 (2026-08-11).
//
// Per entity:
//   1. View-frustum AABB cull against the primary OrthoCameraComponent
//      (world rect centered on the camera; zoom does not change the
//      world extent — mirror of AY2D). No primary camera => fail-open.
//   2. Cache texture/material variants by domain, maps and surface values.
//      SceneOverlay reuses kTilemapPhoskiaSource; WorldLit creates a
//      Material2D whose quad is consumed by the GBuffer geometry substage.
//   3. std::stable_sort by packedSortKey (design.md §7.4 hard rule),
//      then submit — the Forward2DOpaquePass stable-sorts the same
//      key, so item order here IS final draw order.

#include "AYEntity/SpriteRenderSystem.h"

#include "AYEntity/2DUvMath.h"
#include "AYEntity.h"
#include "AYEntity/EntityModule.h"
#include "AYRenderer/RendererSubSystem.h"
#include "AYRenderer/TilemapShaderSources.h"
#include "AYEntity/World.h"

#include "AYEntity/components/OrthoCameraComponent.h"
#include "AYEntity/components/SpriteComponent.h"

#include <AYMath/MathTransform.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ayt::entity
{

namespace {

ayt::render::RenderDomain2D renderDomainOf(const SpriteComponent& sprite) noexcept
{
    return sprite.isWorldLit() ? ayt::render::RenderDomain2D::WorldLit
                               : ayt::render::RenderDomain2D::SceneOverlay;
}

std::string spriteResourceKey(const SpriteComponent& sprite, uint32_t entityId)
{
    if (!sprite.isWorldLit()) {
        return std::string("overlay\x1f") + sprite.texturePath;
    }

    std::string key = "world-lit\x1f";
    const auto append = [&key](const std::string& value) {
        key += value;
        key.push_back('\x1f');
    };
    append(sprite.texturePath);
    append(sprite.normalTexturePath);
    append(sprite.roughnessTexturePath);
    append(sprite.emissiveTexturePath);
    // One mutable material per entity/map set: Inspector slider edits update
    // uniform slots in place instead of leaking one cached material per value.
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

void SpriteRenderSystem::onStart()
{
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        std::fprintf(stderr,
                     "[SpriteRenderSystem] RendererSubSystem not registered; "
                     "sprite draws will not be submitted.\n");
        return;
    }
    World* owner = &World::instance();
    rss->addSceneBuilderForOwner(
        owner, [this, owner](ayt::render::RenderScene& scene) {
            if (&World::instance() == owner) buildRenderScene(scene);
        });
    _started = true;
    std::fprintf(stderr, "[SpriteRenderSystem] world-owned scene builder registered\n");
}

void SpriteRenderSystem::buildRenderScene(ayt::render::RenderScene& scene)
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

    if (!_quad.isValid()) {
        _quad = renderer.createUnitQuad();
    }
    const ayt::render::MeshHandle quad = _quad;
    if (!quad.isValid()) {
        return;
    }

    // Cull viewport (world rect) from the primary camera, if any.
    float camCx = 0.0f, camCy = 0.0f, camHalfW = 0.0f, camHalfH = 0.0f;
    bool haveCamera = false;
    {
        World& world = World::instance();
        for (Entity* entity : world.query<OrthoCameraComponent>()) {
            if (entity == nullptr) {
                continue;
            }
            OrthoCameraComponent* cam = entity->getComponent<OrthoCameraComponent>();
            if (cam == nullptr || !cam->isPrimary) {
                continue;
            }
            camCx    = cam->positionX;
            camCy    = cam->positionY;
            camHalfH = cam->viewSize * 0.5f;
            camHalfW = camHalfH * cam->viewportAspectOr();
            haveCamera = true;
            break;
        }
    }

    struct SpriteEntry {
        ayt::render::DrawItem      item;
        ayt::render::DrawPayload2D payload;
    };
    std::vector<SpriteEntry> entries;

    World& world = World::instance();
    for (Entity* entity : world.query<SpriteComponent>()) {
        if (entity == nullptr) {
            continue;
        }
        SpriteComponent* sprite = entity->getComponent<SpriteComponent>();
        if (sprite == nullptr || !sprite->visible || !sprite->isValid()) {
            continue;
        }

        const ayt::render::RenderDomain2D renderDomain = renderDomainOf(*sprite);

        // Exact AABB of a rotated/scaled centered unit quad.
        // WorldLit2D uses the renderer's 3D camera and must not be culled by
        // the unrelated orthographic overlay camera.
        if (haveCamera && renderDomain == ayt::render::RenderDomain2D::SceneOverlay) {
            const float c = std::fabs(std::cos(sprite->rotationZ));
            const float s = std::fabs(std::sin(sprite->rotationZ));
            const float sx = std::fabs(sprite->scaleX);
            const float sy = std::fabs(sprite->scaleY);
            const float halfW = 0.5f * (c * sx + s * sy);
            const float halfH = 0.5f * (s * sx + c * sy);
            if (std::fabs(sprite->position.x - camCx) > camHalfW + halfW
                || std::fabs(sprite->position.y - camCy) > camHalfH + halfH) {
                continue;
            }
        }

        const bool worldLit = renderDomain == ayt::render::RenderDomain2D::WorldLit;
        const std::string resourceKey =
            spriteResourceKey(*sprite, entity->getId());
        CachedSpriteResources& resources = _cache[resourceKey];
        if (!resources.texture.isValid()) {
            resources.texture = worldLit
                ? renderer.loadTexture(sprite->texturePath, /*srgb=*/true)
                : renderer.loadTexture(sprite->texturePath);
            if (!resources.texture.isValid() && frameIndex < 5) {
                std::fprintf(stderr, "[SpriteRenderSystem] loadTexture failed: '%s'\n",
                             sprite->texturePath.c_str());
            }
        }
        const auto loadOptionalMap = [&](const std::string& path,
                                         ayt::render::TextureHandle& texture,
                                         bool srgb) {
            if (path.empty() || texture.isValid()) {
                return true;
            }
            texture = renderer.loadTexture(path, srgb);
            if (!texture.isValid() && frameIndex < 5) {
                std::fprintf(stderr,
                             "[SpriteRenderSystem] optional map load failed: '%s'\n",
                             path.c_str());
            }
            return texture.isValid();
        };
        bool optionalMapsReady = true;
        if (worldLit) {
            optionalMapsReady =
                loadOptionalMap(sprite->normalTexturePath,
                                resources.normalTexture, /*srgb=*/false)
                && loadOptionalMap(sprite->roughnessTexturePath,
                                   resources.roughnessTexture, /*srgb=*/false)
                && loadOptionalMap(sprite->emissiveTexturePath,
                                   resources.emissiveTexture, /*srgb=*/true);
        }
        if (!resources.texture.isValid() || !optionalMapsReady) {
            continue;
        }
        if (!resources.material.isValid()) {
            if (worldLit) {
                ayt::render::Material2DDesc materialDesc;
                materialDesc.albedo = resources.texture;
                materialDesc.normal = resources.normalTexture;
                materialDesc.roughnessMap = resources.roughnessTexture;
                materialDesc.emissiveMap = resources.emissiveTexture;
                materialDesc.metallic = sprite->metallic;
                materialDesc.roughness = sprite->roughness;
                materialDesc.ambientOcclusion = sprite->ambientOcclusion;
                materialDesc.emissiveStrength = sprite->emissiveStrength;
                materialDesc.alphaCutoff = sprite->alphaCutoff;
                materialDesc.alphaMode = ayt::render::Material2DAlphaMode::Cutout;
                materialDesc.invertNormalY = sprite->invertNormalY;
                materialDesc.doubleSided = true;
                resources.material = renderer.createMaterial2D(
                    materialDesc, resourceKey + "material");
            } else {
                resources.material = renderer.createMaterialFromPhoskia(
                    ayt::render::kTilemapPhoskiaSource, resourceKey + "material");
                if (resources.material.isValid()) {
                    renderer.setMaterialTexture(resources.material, "albedoMap",
                                                resources.texture);
                }
            }
            if (!resources.material.isValid() && frameIndex < 5) {
                std::fprintf(stderr,
                             "[SpriteRenderSystem] material compile failed "
                             "(shaderc missing?)\n");
            }
        }
        // Material validity alone is not enough: createMaterialFromPhoskia
        // only compiles the shader, so a sprite whose texture failed to
        // load would submit an item with no albedo. A broken texture
        // must produce zero items (CM-3 lazy-load failure contract).
        if (!resources.texture.isValid() || !resources.material.isValid()) {
            continue;
        }
        if (worldLit) {
            const float metallic = finiteClamped(
                sprite->metallic, 0.0f, 0.0f, 1.0f);
            const float roughness = finiteClamped(
                sprite->roughness, 0.75f, 0.045f, 1.0f);
            const float ao = finiteClamped(
                sprite->ambientOcclusion, 1.0f, 0.0f, 1.0f);
            const float emissive = finiteClamped(
                sprite->emissiveStrength, 0.0f, 0.0f, 64.0f);
            const float alphaCutoff = finiteClamped(
                sprite->alphaCutoff, 0.5f, 0.0f, 1.0f);
            renderer.setMaterialFloat(resources.material, "metallic", metallic);
            renderer.setMaterialFloat(resources.material, "roughness", roughness);
            renderer.setMaterialFloat(resources.material, "ao", ao);
            renderer.setMaterialFloat(
                resources.material, "normalYSign",
                sprite->invertNormalY ? -1.0f : 1.0f);
            renderer.setMaterialVec3(
                resources.material, "emissive", emissive, emissive, emissive);
            renderer.setMaterialSurfaceProperties(
                resources.material,
                static_cast<int>(ayt::render::Material2DAlphaMode::Cutout),
                alphaCutoff, /*doubleSided=*/true);
        }

        SpriteEntry entry;
        entry.payload.sourceRectMin = sprite->sourceRectMin;
        entry.payload.sourceRectMax = sprite->sourceRectMax;
        entry.payload.tintRGBA      = sprite->colorRGBA;
        entry.payload.flip          = static_cast<uint8_t>(sprite->flip);
        entry.payload.packedSortKey = drawSortKey(sprite->layer, sprite->sortingKey);
        entry.payload.renderDomain  = renderDomain;

        entry.item.mesh     = quad;
        entry.item.material = resources.material;
        entry.item.world    = ayt::math::Transform::getMatrix(
            sprite->position,
            ayt::math::FQuaternion::fromAxisAngle(
                ayt::math::FVector3(0.0f, 0.0f, 1.0f), sprite->rotationZ),
            ayt::math::FVector3(sprite->scaleX, sprite->scaleY, 1.0f));
        entry.item.shadowFlags = worldLit
            ? ayt::render::makeShadowFlags(sprite->castShadow, /*receive=*/true)
            : ayt::render::ShadowFlags::None;
        entries.push_back(entry);
    }

    // design.md §7.4 hard rule: ascending packedSortKey; stable keeps
    // author order for equal keys (same layer + same sortingKey).
    std::stable_sort(entries.begin(), entries.end(),
                     [](const SpriteEntry& a, const SpriteEntry& b) {
                         return a.payload.packedSortKey < b.payload.packedSortKey;
                     });

    _payloads.reserve(entries.size());
    for (SpriteEntry& entry : entries) {
        _payloads.push_back(entry.payload);
        entry.item.payload = &_payloads.back();
        scene.add(entry.item);
    }
}

void registerSpriteRenderSystem()
{
    World::instance().registerSystem<SpriteRenderSystem>(
        SpriteRenderSystem::kPriority);
}

} // namespace ayt::entity
