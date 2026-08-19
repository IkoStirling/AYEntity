// AYSpriteRenderSystem.cpp — CM-3 (2026-08-11).
//
// Per entity:
//   1. View-frustum AABB cull against the primary OrthoCameraComponent
//      (world rect centered on the camera; zoom does not change the
//      world extent — mirror of AY2D). No primary camera => fail-open.
//   2. Cache the texture + kTilemapPhoskiaSource material (path-keyed;
//      the sprite reuses the tilemap shader — both go through the same
//      srcRect/tint/flip uniforms, pass has zero branches).
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

namespace ayt::entity
{

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
    rss->setSceneBuilder([this](ayt::render::RenderScene& scene) {
        buildRenderScene(scene);
    });
    _started = true;
    std::fprintf(stderr, "[SpriteRenderSystem] scene builder registered\n");
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

        // AABB cull: sprite half-extent from |scale| (rotation ignored
        // — a conservative approximation for small sprites; exact
        // rotated AABBs are a Phase 6 budget item).
        if (haveCamera) {
            const float halfW = std::fabs(sprite->scaleX) * 0.5f;
            const float halfH = std::fabs(sprite->scaleY) * 0.5f;
            if (std::fabs(sprite->position.x - camCx) > camHalfW + halfW
                || std::fabs(sprite->position.y - camCy) > camHalfH + halfH) {
                continue;
            }
        }

        CachedSpriteResources& resources = _cache[sprite->texturePath];
        if (!resources.texture.isValid()) {
            resources.texture = renderer.loadTexture(sprite->texturePath);
            if (!resources.texture.isValid() && frameIndex < 5) {
                std::fprintf(stderr, "[SpriteRenderSystem] loadTexture failed: '%s'\n",
                             sprite->texturePath.c_str());
            }
        }
        if (!resources.material.isValid()) {
            resources.material = renderer.createMaterialFromPhoskia(
                ayt::render::kTilemapPhoskiaSource, sprite->texturePath);
            if (resources.material.isValid()) {
                renderer.setMaterialTexture(resources.material, "albedoMap",
                                            resources.texture);
            } else if (frameIndex < 5) {
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

        SpriteEntry entry;
        entry.payload.sourceRectMin = sprite->sourceRectMin;
        entry.payload.sourceRectMax = sprite->sourceRectMax;
        entry.payload.tintRGBA      = sprite->colorRGBA;
        entry.payload.flip          = static_cast<uint8_t>(sprite->flip);
        entry.payload.packedSortKey = drawSortKey(sprite->layer, sprite->sortingKey);

        entry.item.mesh     = quad;
        entry.item.material = resources.material;
        entry.item.world    = ayt::math::Transform::getMatrix(
            sprite->position,
            ayt::math::FQuaternion::fromAxisAngle(
                ayt::math::FVector3(0.0f, 0.0f, 1.0f), sprite->rotationZ),
            ayt::math::FVector3(sprite->scaleX, sprite->scaleY, 1.0f));
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
