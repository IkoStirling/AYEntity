#include "AYEntity/RenderSystem.h"
#include <AYEntity/SceneLighting.h>



#include "AYEntity.h"

#include "AYEntity/EntityModule.h"

#include "AYRenderer/RendererSubSystem.h"
#include <AYRenderer/MeshSubmissionInfo.h>

#include "AYEntity/components/MeshComponent.h"

#include "AYEntity/components/TransformComponent.h"



#include "AYMath/MathTransform.h"
#include <AYResource/AssetPath.h>
#include <AYIO/Path.h>

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <vector>



namespace ayt::entity

{



namespace {



struct CachedDrawResources {

    ayt::render::MeshHandle     mesh;

    uint64_t extraction=0;
    std::shared_ptr<const ayt::render::MeshSubmissionInfo> snapshot;

    struct Section {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        std::string materialPath;
        ayt::render::MaterialHandle material;
    };
    std::vector<Section> sections;
    std::string firstSlotPath;

};



ayt::math::Float4x4 transformToWorldMatrix(const Entity& entity)

{
    const float alpha = ayt::game::GameLoop::instance().getInterpolationFactor();
    return entity.worldMatrix(alpha);

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

struct RenderSystem::ResourceCache {
    struct Path {std::string resolved;uint64_t extraction=0;bool absolute=false;};
    struct Material {ayt::render::MaterialHandle handle;uint64_t extraction=0;};
    uint64_t context=0,extraction=0;
    std::vector<std::string> roots;
    std::unordered_map<std::string,CachedDrawResources> entries;
    std::unordered_map<std::string,Path> paths;
    std::unordered_map<std::string,Material> materials;
    const std::string empty;
    void clear(){entries.clear();paths.clear();materials.clear();}
};

RenderSystem::RenderSystem()=default;
RenderSystem::~RenderSystem()=default;


void RenderSystem::onStart()

{
    _resources.reset();

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
    ayt::render::RendererSubSystem* rendererSubSystem =
        ayt::render::RendererSubSystem::findRegistered();
    ayt::render::SceneLights authoredLights;
    const auto lighting=collectSceneLighting(World::instance(),authoredLights,
        ayt::game::GameLoop::instance().getInterpolationFactor(),
        rendererSubSystem ? rendererSubSystem->activeCameraSpaceRegion() : 0);
    if(lighting.authored) scene.setSceneLights(authoredLights);


    static uint32_t s_frameIndex = 0;

    const uint32_t frameIndex = s_frameIndex++;



    if (rendererSubSystem == nullptr) {

        if (frameIndex < 3) {

            std::fprintf(stderr, "[RenderSystem] RendererSubSystem missing during build\n");

        }

        return;

    }



    ayt::render::Renderer& renderer = rendererSubSystem->renderer();

    if(!_resources)_resources=std::make_unique<ResourceCache>();
    const auto context=renderer.resourceContextId();
    const auto& roots=ayt::resource::assetRoots();
    if(_resources->context!=context||_resources->roots!=roots){
        _resources->clear();_resources->context=context;_resources->roots=roots;
    }
    if(!context)return;
    if(++_resources->extraction==0){_resources->clear();_resources->extraction=1;}
    const uint64_t extraction=_resources->extraction;
    // Reuse cache nodes, but validate current resources once per invocation,
    // including separate camera-view extractions in the same renderer frame.
    const auto resolvePath=[&](const std::string& path)->const std::string&{
        if(path.empty())return _resources->empty;
        auto [found,inserted]=_resources->paths.try_emplace(path);
        auto& record=found->second;
        if(inserted){record.absolute=ayt::io::path::isAbsolute(path);}
        if(inserted||(!record.absolute&&record.extraction!=extraction)){
            record.resolved=ayt::resource::resolveAssetPath({},path);record.extraction=extraction;
        }
        return record.resolved;
    };
    const auto resolveMaterial=[&](const std::string& path){
        if(path.empty())return ayt::render::MaterialHandle{};
        auto& record=_resources->materials[path];
        if(record.extraction!=extraction){
            record.extraction=extraction;
            if(!renderer.isMaterialResident(record.handle))record.handle=renderer.loadMaterial(path);
        }
        return record.handle;
    };

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


        const std::string& meshPath=resolvePath(meshComp->meshPath);
        const std::string& materialPath=resolvePath(meshComp->materialPath);
        auto& resources=_resources->entries[meshPath];
        if(resources.extraction!=extraction) {
            resources.extraction=extraction;
            auto snapshot=renderer.meshSubmissionInfo(resources.mesh);
            if(!snapshot) {
                resources.mesh=renderer.loadMesh(meshPath);
                snapshot=renderer.meshSubmissionInfo(resources.mesh);
            }
            if(snapshot!=resources.snapshot) {
                std::vector<CachedDrawResources::Section> sections;
                std::string firstSlotPath;
                if(snapshot&&snapshot->sections.size()>1) {
                    sections.reserve(snapshot->sections.size());
                    if(!snapshot->materialSlots.empty()&&!snapshot->materialSlots[0].empty())
                        firstSlotPath=ayt::resource::resolveAssetPath(meshPath,snapshot->materialSlots[0]);
                    for(const auto& source:snapshot->sections) {
                        if(!source.indexCount)continue;
                        CachedDrawResources::Section section;
                        section.firstIndex=source.firstIndex;section.indexCount=source.indexCount;
                        if(source.materialIndex<snapshot->materialSlots.size()&&!snapshot->materialSlots[source.materialIndex].empty())
                            section.materialPath=ayt::resource::resolveAssetPath(meshPath,snapshot->materialSlots[source.materialIndex]);
                        sections.push_back(std::move(section));
                    }
                }
                // Publish derived metadata only after all allocation succeeds.
                resources.snapshot=snapshot;resources.sections=std::move(sections);
                resources.firstSlotPath=std::move(firstSlotPath);
            }
            for(auto& section:resources.sections)section.material=resolveMaterial(section.materialPath);
            if(!resources.mesh.isValid()&&frameIndex<5)
                std::fprintf(stderr,"[RenderSystem] loadMesh failed: '%s'\n",meshComp->meshPath.c_str());
        }
        const auto material=resolveMaterial(materialPath);
        if(!materialPath.empty()&&!material.isValid()&&frameIndex<5)
            std::fprintf(stderr,"[RenderSystem] loadMaterial failed: '%s'\n",meshComp->materialPath.c_str());

        const bool useSections = !meshComp->outlineHull && !resources.sections.empty()
            && (materialPath.empty() || materialPath == resources.firstSlotPath);
        if (!resources.mesh.isValid() || (!material.isValid() && !useSections)) {

            ++skippedLoad;

            continue;

        }

        // .aymat has no blend field — MeshComponent::alphaBlend is the host tag.
        if (meshComp->alphaBlend) {
            renderer.setMaterialBlendMode(material,
                                          ayt::render::BlendMode::Alpha);
        }

        ayt::render::DrawItem item;
        item.sourceEntityId=entity->getId();
        item.sourceWorld=reinterpret_cast<uintptr_t>(&world);
        item.mesh         = resources.mesh;
        item.material     = material;
        item.world        = transformToWorldMatrix(*entity);
        item.spaceRegionId = entity->effectiveSpaceRegion();
        // Temporal passes match this rigid draw to its previous transform.
        item.motionObjectId = temporalObjectIdentity(world, *entity);
        item.cameraLayerMask = meshComp->layer >= 0 && meshComp->layer < 32 ? 1u << meshComp->layer : 0;
        item.shadowFlags  = ayt::render::makeShadowFlags(meshComp->castShadow,
                                                         meshComp->receiveShadow);
        item.outlineHull  = meshComp->outlineHull;
        if (meshComp->outlineHull && meshComp->hasOutlineSourceScale) {
            Transform depthXf = *transform;
            depthXf.scale = meshComp->outlineSourceScale;
            item.hasOutlineDepthWorld = true;
            const float alpha = ayt::game::GameLoop::instance().getInterpolationFactor();
            item.outlineDepthWorld = (entity->spatialParent() ? entity->spatialParent()->worldMatrix(alpha)
                : ayt::math::Float4x4::identity()) * ayt::math::Transform::getMatrix(
                    depthXf.interpolatedPosition(alpha),depthXf.interpolatedRotation(alpha),depthXf.scale);
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

        if (useSections) {
            // The editor initializes materialPath to slot zero. This is a
            // fallback, not an instruction to paint every section with it.
            // A different explicit material (including selection outlines)
            // retains the legacy whole-mesh override behavior.
            for (auto& section : resources.sections) {
                auto draw = item;
                draw.firstIndex = section.firstIndex;
                draw.indexCount = section.indexCount;
                draw.material = section.material.isValid() ? section.material : material;
                if (!draw.material.isValid()) { ++skippedLoad; continue; }
                if (meshComp->alphaBlend)
                    renderer.setMaterialBlendMode(draw.material, ayt::render::BlendMode::Alpha);
                scene.add(draw);
                ++submitted;
            }
        } else {
            scene.add(item);
            ++submitted;
        }

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
