#include "AYRenderSystem.h"



#include "AYEntity.h"

#include "AYEntityModule.h"

#include "AYRendererSubSystem.h"

#include "components/AYMeshComponent.h"

#include "components/AYTransformComponent.h"



#include "aymath/MathTransform.h"

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

    return ayt::math::Transform::getMatrix(transform.position, transform.rotation, transform.scale);

}



std::string assetKey(const std::string& meshPath, const std::string& materialPath)

{

    return meshPath + "|" + materialPath;

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



    rendererSubSystem->setSceneBuilder(

        [this](ayt::render::RenderScene& scene) { buildRenderScene(scene); });

    _started = true;

    std::fprintf(stderr, "[RenderSystem] scene builder registered\n");

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

