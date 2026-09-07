#include "AYEntity/RenderSystem.h"



#include "AYEntity.h"

#include "AYEntity/EntityModule.h"

#include "AYRenderer/RendererSubSystem.h"

#include "AYEntity/components/MeshComponent.h"

#include "AYEntity/components/TransformComponent.h"



#include "AYMath/MathTransform.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>



namespace ayt::entity

{



namespace {



struct CachedDrawResources {

    ayt::render::MeshHandle     mesh;

    ayt::render::MaterialHandle material;

};



ayt::math::Float4x4 transformToWorldMatrix(const Transform& transform)

{
    const float alpha = ayt::game::GameLoop::instance().getInterpolationFactor();
    return ayt::math::Transform::getMatrix(transform.interpolatedPosition(alpha),
                                           transform.interpolatedRotation(alpha),
                                           transform.scale);

}



std::string assetKey(const std::string& meshPath, const std::string& materialPath)

{

    return meshPath + "|" + materialPath;

}

uint64_t temporalObjectIdentity(World& world, Entity& entity) noexcept
{
    // Entity ids restart in a different Edit/Play World. Mix world identity
    // and handle generation so a scene switch or recycled id cannot inherit
    // another object's transform history.
    const EntityHandle handle = world.getEntityHandle(entity.getId());
    uint64_t value = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(&world));
    value ^= static_cast<uint64_t>(handle.id) + 0x9e3779b97f4a7c15ull
          + (value << 6u) + (value >> 2u);
    value ^= static_cast<uint64_t>(handle.version) << 32u;
    return value != 0u ? value : 1u;
}



void logBuildSceneSummary(uint32_t frameIndex, uint32_t matched, uint32_t skippedInvalid,

                          uint32_t skippedLoad, uint32_t submitted, size_t sceneItems)

{

    // Startup-only — periodic spam made Play-mode stderr unreadable.
    if (frameIndex < 5) {

        std::fprintf(stderr,

                     "[RenderSystem] frame=%u matched=%u skipInvalid=%u skipLoad=%u submitted=%u "

                     "sceneItems=%zu\n",

                     frameIndex, matched, skippedInvalid, skippedLoad, submitted, sceneItems);

    }

}



} // namespace



void RenderSystem::onStart()

{

    ayt::render::RendererSubSystem* rendererSubSystem =

        ayt::render::RendererSubSystem::findRegistered();

    if (rendererSubSystem == nullptr) {

        std::fprintf(stderr, "[RenderSystem] RendererSubSystem not registered\n");

        return;

    }



    World* owner = &World::instance();
    rendererSubSystem->addSceneBuilderForOwner(
        owner, [this, owner](ayt::render::RenderScene& scene) {
            if (&World::instance() == owner) buildRenderScene(scene);
        });

    _started = true;

    std::fprintf(stderr, "[RenderSystem] world-owned scene builder registered\n");

}



void RenderSystem::onUpdate(float /*dt*/)

{

}



void RenderSystem::buildRenderScene(ayt::render::RenderScene& scene)

{

    static uint32_t s_frameIndex = 0;

    const uint32_t frameIndex = s_frameIndex++;



    ayt::render::RendererSubSystem* rendererSubSystem =

        ayt::render::RendererSubSystem::findRegistered();

    if (rendererSubSystem == nullptr) {

        if (frameIndex < 3) {

            std::fprintf(stderr, "[RenderSystem] RendererSubSystem missing during build\n");

        }

        return;

    }



    ayt::render::Renderer& renderer = rendererSubSystem->renderer();

    static std::unordered_map<std::string, CachedDrawResources> cache;



    uint32_t matched = 0;

    uint32_t skippedInvalid = 0;

    uint32_t skippedLoad = 0;

    uint32_t skippedSkinned = 0;  // Phase 1 SC-01: routed to SkinnedMeshRenderSystem.

    uint32_t submitted = 0;



    World& world = World::instance();

    for (Entity* entity : world.query<Transform, MeshComponent>()) {

        if (entity == nullptr) {

            continue;

        }

        ++matched;



        Transform*     transform = entity->getComponent<Transform>();

        MeshComponent* meshComp  = entity->getComponent<MeshComponent>();

        if (transform == nullptr || meshComp == nullptr || !meshComp->visible

            || !meshComp->isValid()) {

            ++skippedInvalid;
            continue;
        }

        // Phase 1 SC-01: skinned entities are owned by
        // SkinnedMeshRenderSystem (separate scene-builder callback).
        // Skip them here so the two passes don't double-submit the
        // same draw.
        if (meshComp->skinned) {
            ++skippedSkinned;
            continue;
        }


        const std::string key = assetKey(meshComp->meshPath, meshComp->materialPath);

        CachedDrawResources& resources = cache[key];

        if (!resources.mesh.isValid()) {

            resources.mesh = renderer.loadMesh(meshComp->meshPath);

            if (!resources.mesh.isValid() && frameIndex < 5) {

                std::fprintf(stderr, "[RenderSystem] loadMesh failed: '%s'\n",

                             meshComp->meshPath.c_str());

            }

        }

        if (!resources.material.isValid() && !meshComp->materialPath.empty()) {

            resources.material = renderer.loadMaterial(meshComp->materialPath);

            if (!resources.material.isValid() && frameIndex < 5) {

                std::fprintf(stderr, "[RenderSystem] loadMaterial failed: '%s'\n",

                             meshComp->materialPath.c_str());

            }

        }



        if (!resources.mesh.isValid() || !resources.material.isValid()) {

            ++skippedLoad;

            continue;

        }

        // .aymat has no blend field — MeshComponent::alphaBlend is the host tag.
        if (meshComp->alphaBlend) {
            renderer.setMaterialBlendMode(resources.material,
                                          ayt::render::BlendMode::Alpha);
        }

        ayt::render::DrawItem item;
        item.mesh         = resources.mesh;
        item.material     = resources.material;
        item.world        = transformToWorldMatrix(*transform);
        // Temporal passes match this rigid draw to its previous transform.
        item.motionObjectId = temporalObjectIdentity(world, *entity);
        item.shadowFlags  = ayt::render::makeShadowFlags(meshComp->castShadow,
                                                         meshComp->receiveShadow);
        item.outlineHull  = meshComp->outlineHull;
        if (meshComp->outlineHull && meshComp->hasOutlineSourceScale) {
            Transform depthXf = *transform;
            depthXf.scale = meshComp->outlineSourceScale;
            item.hasOutlineDepthWorld = true;
            item.outlineDepthWorld = transformToWorldMatrix(depthXf);
        }

        // TransparentPass sorts descending by sortKey (far → near).
        // Distance² × 100 → int; farther objects get larger keys.
        //
        // L2 (lh-rh-split-entity audit 2026-08-24): reads the world
        // translation by assuming Float4x4 stores translation in
        // row[0..2].w (row-major math convention — same layout as
        // Float4x4::decompose, see AYMath/MathTypes.h:842). AYMath
        // does not expose a translation-only accessor, and
        // switching to decompose() would also pull rotation/scale —
        // overkill for a squared-distance sort. If Float4x4 storage
        // ever flips to column-major, replace the three reads below
        // with a single decompose() call.
        {
            const ayt::math::FVector3 cam = renderer.mainCameraPosition();
            const float tx = item.world.row[0].w;
            const float ty = item.world.row[1].w;
            const float tz = item.world.row[2].w;
            const float dx = tx - cam.x;
            const float dy = ty - cam.y;
            const float dz = tz - cam.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            const float scaled = distSq * 100.0f;
            item.sortKey = static_cast<int32_t>(
                std::min(scaled, 2.0e9f));
        }

        scene.add(item);

        ++submitted;

    }



    logBuildSceneSummary(frameIndex, matched, skippedInvalid, skippedLoad, submitted,

                         scene.items().size());
    (void)skippedSkinned;  // reserved for future per-frame diagnostic.

}



void registerRenderSystem()

{

    // GL-01: idempotent across World::shutdown — see the matching
    // comment in AYAnimationSystem.cpp. The bootstrapModule() guard
    // is the only one we need.

    World::instance().registerSystem<RenderSystem>(500);

}



} // namespace ayt::entity
