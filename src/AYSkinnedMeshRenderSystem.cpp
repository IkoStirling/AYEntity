// AYSkinnedMeshRenderSystem.cpp — Phase 1 E-04 implementation.
//
// Submits draws for entities with MeshComponent::skinned == true
// AND a SkeletonComponent. Coexists with RenderSystem in the active
// World's owner-scoped scene-builder group.
//
// Cooked meshes carry one local-to-global bone palette per render section.
// DrawItems borrow the complete runtime pose and the section remap; every
// renderer pass resolves the same palette before submitting.

#include "AYEntity/SkinnedMeshRenderSystem.h"

#include <AYEntity/components/MeshComponent.h>
#include <AYEntity/components/SkeletonComponent.h>

#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYRenderer/RenderScene.h>
#include <AYRenderer.h>
#include <AYRenderer/FallbackMeshShaderSources.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYEntity/World.h>
#include <AYMath/MathTransform.h>
#include <AYMath/MathUtils.h>
#include <AYResource/AssetPath.h>
#include <AYRenderer/MeshSubmissionInfo.h>
#include <AYIO/Env.h>
#include <AYIO/Path.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace ayt::entity
{

namespace {

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

int32_t transparentDistanceSortKey(
    const ayt::math::FVector3& localCenter,
    const ayt::math::Float4x4& world,
    const ayt::math::FVector3& camera) noexcept
{
    const ayt::math::FVector3 center = world.transformPoint(localCenter);
    const double dx = static_cast<double>(center.x) - camera.x;
    const double dy = static_cast<double>(center.y) - camera.y;
    const double dz = static_cast<double>(center.z) - camera.z;
    // Preserve sub-millimetre separation between layered face/eye/clothing
    // meshes while retaining a useful range of roughly 21 km.
    const double scaledDistance = std::sqrt(dx * dx + dy * dy + dz * dz)
                                * 100000.0;
    return static_cast<int32_t>(std::min(
        scaledDistance,
        static_cast<double>(std::numeric_limits<int32_t>::max())));
}

uint64_t temporalObjectIdentity(World& world, Entity& entity) noexcept
{
    const EntityHandle handle = world.getEntityHandle(entity.getId());
    uint64_t value = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(&world));
    value ^= static_cast<uint64_t>(handle.id) + 0x9e3779b97f4a7c15ull
          + (value << 6u) + (value >> 2u);
    value ^= static_cast<uint64_t>(handle.version) << 32u;
    return value != 0u ? value : 1u;
}

using ayt::render::kSkinnedLitFragmentSc;
using ayt::render::kRigidLitVaryingDef;
using ayt::render::kRigidLitVertexSc;

} // namespace

void SkinnedMeshRenderSystem::onStart()
{
    _materialCache.clear();
    _meshSubmissionCache.clear();
    _pathMaterialCache.clear();
    _assetRoots.clear();
    _extractionSerial=0;

    const std::string diagnostic = lowerCopy(
        ayt::io::env::get("AY_SKINNED_DIAGNOSTIC").value_or(""));
    _diagnosticLog = diagnostic == "log" || diagnostic == "1"
                  || diagnostic == "solid" || diagnostic == "solid-doublesided"
                  || diagnostic == "solid_double_sided" || diagnostic == "all";
    _diagnosticSolid = diagnostic == "solid" || diagnostic == "solid-doublesided"
                    || diagnostic == "solid_double_sided" || diagnostic == "all";
    _diagnosticDoubleSided = diagnostic == "solid-doublesided"
                          || diagnostic == "solid_double_sided" || diagnostic == "all";
    if (_diagnosticLog) {
        std::fprintf(stderr,
                     "[SkinnedMeshDiagnostic] mode=%s solid=%d doubleSided=%d\n",
                     diagnostic.c_str(), _diagnosticSolid ? 1 : 0,
                     _diagnosticDoubleSided ? 1 : 0);
    }

    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        std::fprintf(stderr,
                     "[SkinnedMeshRenderSystem] RendererSubSystem not registered; "
                     "skinned draws will not be submitted.\n");
        return;
    }

    World* owner = &World::instance();
    rss->addSceneBuilderForOwner(
        owner, [this, owner](ayt::render::RenderScene& scene) {
            if (&World::instance() == owner) buildSkinnedScene(scene);
        });

    _started = true;
    std::fprintf(stderr,
                 "[SkinnedMeshRenderSystem] world-owned scene builder registered\n");
}

void SkinnedMeshRenderSystem::onUpdate(float /*dt*/)
{
}

ayt::render::MaterialHandle SkinnedMeshRenderSystem::loadMaterialCached(
    ayt::render::Renderer& renderer, const std::string& path, ExtractionResources& extraction)
{
    if(path.empty()||(!extraction.failedMaterials.empty()&&extraction.failedMaterials.contains(path)))return {};
    const auto found=_pathMaterialCache.find(path);
    if(found!=_pathMaterialCache.end()){
        if(found->second.extraction==_extractionSerial)return found->second.handle;
        if(renderer.isMaterialResident(found->second.handle)){
            found->second.extraction=_extractionSerial;return found->second.handle;
        }
    }
    const auto material=renderer.loadMaterial(path);
    if(material.isValid())_pathMaterialCache.insert_or_assign(path,CachedPathMaterial{material,_extractionSerial});
    else {_pathMaterialCache.erase(path);extraction.failedMaterials.insert(path);}
    return material;
}

SkinnedMeshRenderSystem::CachedMeshSubmission*
SkinnedMeshRenderSystem::loadMeshSubmissionCached(
    ayt::render::Renderer& renderer, const std::string& meshPath, ExtractionResources& extraction)
{
    if(!extraction.failedMeshes.empty()&&extraction.failedMeshes.contains(meshPath))return nullptr;
    const auto found = _meshSubmissionCache.find(meshPath);
    if(found!=_meshSubmissionCache.end()&&found->second.extraction==_extractionSerial)return &found->second;
    auto mesh=found!=_meshSubmissionCache.end()&&ayt::io::path::isAbsolute(meshPath)
        ?found->second.mesh:ayt::render::MeshHandle{};
    auto snapshot=renderer.meshSubmissionInfo(mesh);
    if(!snapshot){mesh=renderer.loadMesh(meshPath);snapshot=renderer.meshSubmissionInfo(mesh);}
    if(!mesh.isValid()||!snapshot){
        _meshSubmissionCache.erase(meshPath);extraction.failedMeshes.insert(meshPath);return nullptr;
    }
    if(found!=_meshSubmissionCache.end()&&found->second.mesh.id==mesh.id&&found->second.snapshot==snapshot){
        for(auto& section:found->second.submeshes)
            if(!renderer.isMaterialResident(section.material))
                section.material=loadMaterialCached(renderer,section.materialPath,extraction);
        found->second.extraction=_extractionSerial;
        return &found->second;
    }

    CachedMeshSubmission cached;
    cached.mesh = mesh;
    cached.snapshot = snapshot;
    cached.extraction=_extractionSerial;
    const auto submeshCount = static_cast<uint32_t>(snapshot->sections.size());
    const auto slotCount = static_cast<uint32_t>(snapshot->materialSlots.size());
    bool legacyOrdinalSlots = submeshCount > 1 && submeshCount == slotCount;
    for (const auto& section : snapshot->sections)
        legacyOrdinalSlots = legacyOrdinalSlots && section.materialIndex == 0;
    if (legacyOrdinalSlots) {
        static bool logged = false;
        if (!logged) {
            std::fprintf(stderr, "[SkinnedMeshRenderSystem] legacy submesh material indices; using slot order\n");
            logged = true;
        }
    }

    cached.submeshes.reserve(submeshCount);
    uint64_t coveredIndices = 0;
    for (uint32_t i = 0; i < submeshCount; ++i) {
        const auto& source = snapshot->sections[i];
        coveredIndices += source.indexCount;
        if (!source.indexCount) continue;
        CachedSubmeshSubmission section;
        section.firstIndex = source.firstIndex;
        section.indexCount = source.indexCount;
        section.sourceMaterialIndex = source.materialIndex;
        section.localCenter = source.localCenter;
        section.bonePalette = source.bonePalette;
        const uint32_t slot = legacyOrdinalSlots ? i : source.materialIndex;
        if (slot < slotCount && !snapshot->materialSlots[slot].empty()) {
            section.materialPath = ayt::resource::resolveAssetPath(meshPath,snapshot->materialSlots[slot]);
            section.material = loadMaterialCached(renderer,section.materialPath,extraction);
        }
        if (_diagnosticLog) {
            std::fprintf(stderr,"[SkinnedMeshDiagnostic] mesh=%s submesh=%u first=%u count=%u materialIndex=%u slot=%u material=%s loaded=%d\n",
                meshPath.c_str(),i,section.firstIndex,section.indexCount,source.materialIndex,slot,
                section.materialPath.c_str(),section.material.isValid()?1:0);
        }
        cached.submeshes.push_back(std::move(section));
    }
    if (_diagnosticLog) {
        std::fprintf(stderr,"[SkinnedMeshDiagnostic] uploaded mesh=%s vertices=%u indices=%u submeshes=%u slots=%u coveredIndices=%llu\n",
            meshPath.c_str(),snapshot->vertexCount,snapshot->indexCount,submeshCount,slotCount,
            static_cast<unsigned long long>(coveredIndices));
    }
    auto [inserted, unused] = _meshSubmissionCache.insert_or_assign(meshPath,std::move(cached));
    (void)unused;
    return &inserted->second;
}

void SkinnedMeshRenderSystem::buildSkinnedScene(ayt::render::RenderScene& scene)
{
    if (!_started) return;

    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) return;
    ayt::render::Renderer& renderer = rss->renderer();
    const uint64_t context = renderer.resourceContextId();
    if (!context) return;
    const auto& roots=ayt::resource::assetRoots();
    if (_resourceContextId != context||_assetRoots!=roots) {
        // Generated fallback instances are context-owned, independent of the
        // mounted asset roots. Do not orphan them on a roots-only change.
        if(_resourceContextId!=context)_materialCache.clear();
        _meshSubmissionCache.clear();
        _pathMaterialCache.clear();
        _assetRoots=roots;
        _resourceContextId = context;
    }
    if(++_extractionSerial==0){_meshSubmissionCache.clear();_pathMaterialCache.clear();_extractionSerial=1;}
    const ayt::math::FVector3 cameraPosition = renderer.mainCameraPosition();
    ExtractionResources extraction;

    constexpr const char* kRigidKey = "AYEntity_RigidLit_bgfx_v2";
    const MaterialKey rigidKey{ kRigidKey };
    auto rigidIt = _materialCache.find(rigidKey);
    if (rigidIt == _materialCache.end()) {
        const ayt::render::MaterialHandle h =
            renderer.createMaterialFromBgfxSc(kRigidLitVertexSc,
                                              kSkinnedLitFragmentSc,
                                              kRigidLitVaryingDef,
                                              kRigidKey);
        if (!h.isValid()) {
            std::fprintf(stderr,
                         "[SkinnedMeshRenderSystem] RigidLit compile failed\n");
            std::fflush(stderr);
            return;
        }
        rigidIt = _materialCache.emplace(rigidKey, h).first;
        std::fprintf(stderr,
                     "[SkinnedMeshRenderSystem] RigidLit ready (bind-pose / Deferred)\n");
        std::fflush(stderr);
    }

    World& world = World::instance();
    uint32_t submitted = 0;
    uint32_t skippedNoMesh = 0;
    for (Entity* e : world.query<Transform, MeshComponent, SkeletonComponent>()) {
        if (e == nullptr) continue;
        Transform*         transform = e->getComponent<Transform>();
        MeshComponent*     meshComp  = e->getComponent<MeshComponent>();
        SkeletonComponent* skel      = e->getComponent<SkeletonComponent>();
        if (transform == nullptr || meshComp == nullptr || skel == nullptr) continue;
        if (!meshComp->skinned) continue;
        if (!meshComp->visible || meshComp->meshPath.empty()) continue;

        CachedMeshSubmission* meshSubmission =
            loadMeshSubmissionCached(renderer, meshComp->meshPath,extraction);
        if (meshSubmission == nullptr || !meshSubmission->mesh.isValid()) {
            ++skippedNoMesh;
            static uint32_t s_meshFailLog = 0;
            if (s_meshFailLog < 3) {
                std::fprintf(stderr,
                             "[SkinnedMeshRenderSystem] loadMesh('%s') failed\n",
                             meshComp->meshPath.c_str());
                std::fflush(stderr);
                ++s_meshFailLog;
            }
            continue;
        }

        // This path is a whole-mesh fallback only. Multi-material imported
        // meshes are expanded into one DrawItem per submesh below.
        ayt::render::MaterialHandle fallbackMat = rigidIt->second;
        if (!meshComp->materialPath.empty()) {
            const ayt::render::MaterialHandle cooked =
                loadMaterialCached(renderer, meshComp->materialPath,extraction);
            if (cooked.isValid()) {
                fallbackMat = cooked;
            } else {
                static uint32_t s_matFailLog = 0;
                if (s_matFailLog < 3) {
                    std::fprintf(stderr,
                                 "[SkinnedMeshRenderSystem] loadMaterial('%s') "
                                 "failed — RigidLit fallback\n",
                                 meshComp->materialPath.c_str());
                    std::fflush(stderr);
                    ++s_matFailLog;
                }
                renderer.setMaterialColor(rigidIt->second, "baseColor",
                                          0.92f, 0.78f, 0.55f, 1.0f);
            }
        } else {
            renderer.setMaterialColor(rigidIt->second, "baseColor",
                                      0.92f, 0.78f, 0.55f, 1.0f);
        }

        const float interpolationAlpha =
            ayt::game::GameLoop::instance().getInterpolationFactor();
        const ayt::math::Float4x4 worldM =
            e->worldMatrix(interpolationAlpha);

        bool submittedSubmesh = false;
        uint32_t submeshOrdinal = 0;
        for (const CachedSubmeshSubmission& submesh : meshSubmission->submeshes) {
            const ayt::render::MaterialHandle submeshMat =
                _diagnosticSolid
                    ? rigidIt->second
                    : (submesh.material.isValid() ? submesh.material : fallbackMat);
            ayt::render::DrawItem item;
        item.sourceEntityId=e->getId();
        item.sourceWorld=reinterpret_cast<uintptr_t>(&world);
            item.mesh = meshSubmission->mesh;
            item.material = submeshMat;
            item.firstIndex = submesh.firstIndex;
            item.indexCount = submesh.indexCount;
            item.world = worldM;
            item.spaceRegionId = e->effectiveSpaceRegion();
            // All sections share one entity identity. MotionVectorPass adds
            // mesh identity internally, then snapshots the complete skeleton
            // once all sections have consumed the previous pose.
            item.motionObjectId = temporalObjectIdentity(world, *e);
            item.cameraLayerMask = meshComp->layer >= 0 && meshComp->layer < 32 ? 1u << meshComp->layer : 0;
            item.outlineHull = meshComp->outlineHull;
            item.sortKey = transparentDistanceSortKey(
                submesh.localCenter, worldM, cameraPosition);
            if (skel->loaded && skel->skinMatrices != nullptr && skel->jointCount > 0u) {
                item.boneMatrices = skel->skinMatrices;
                item.skeletonJointCount = skel->jointCount;
                if (!submesh.bonePalette.empty()) {
                    item.boneRemap = submesh.bonePalette.data();
                    item.jointCount = static_cast<uint32_t>(submesh.bonePalette.size());
                } else if (skel->jointCount <= ayt::render::kUniformSkinPaletteCapacity) {
                    // Legacy/procedural mesh whose vertex indices already
                    // address the first matrices directly.
                    item.jointCount = skel->jointCount;
                } else {
                    static uint32_t s_missingPaletteLog = 0u;
                    if (s_missingPaletteLog < 3u) {
                        std::fprintf(stderr,
                                     "[SkinnedMeshRenderSystem] mesh '%s' has %u joints but no "
                                     "section palette; draw uses bind pose\n",
                                     meshComp->meshPath.c_str(), skel->jointCount);
                        ++s_missingPaletteLog;
                    }
                    item.boneMatrices = nullptr;
                    item.skeletonJointCount = 0u;
                }
            }
            scene.add(item);
            ++submitted;
            submittedSubmesh = true;
            if (_diagnosticLog && _diagnosticSolid && submeshOrdinal < 3) {
                std::fprintf(stderr,
                             "[SkinnedMeshDiagnostic] draw submesh=%u "
                             "first=%u count=%u forced=solid\n",
                             submeshOrdinal, submesh.firstIndex, submesh.indexCount);
            }
            ++submeshOrdinal;
        }

        if (_diagnosticSolid && _diagnosticDoubleSided) {
            renderer.setMaterialSurfaceProperties(rigidIt->second, 0, 0.5f, true);
        }

        if (!submittedSubmesh) {
            ayt::render::DrawItem item;
        item.sourceEntityId=e->getId();
        item.sourceWorld=reinterpret_cast<uintptr_t>(&world);
            item.mesh = meshSubmission->mesh;
            item.material = fallbackMat;
            item.world = worldM;
            item.spaceRegionId = e->effectiveSpaceRegion();
            item.motionObjectId = temporalObjectIdentity(world, *e);
            item.cameraLayerMask = meshComp->layer >= 0 && meshComp->layer < 32 ? 1u << meshComp->layer : 0;
            scene.add(item);
            ++submitted;
        }

        static bool s_once = false;
        if (!s_once) {
            std::fprintf(stderr,
                         "[SkinnedMeshRenderSystem] skinned submit "
                         "(loaded=%d joints=%u scale=%.4f pos=(%.2f,%.2f,%.2f))\n"
                         "  mesh=%s\n",
                         skel->loaded ? 1 : 0, skel->jointCount,
                         transform->scale.x,
                         transform->position.x, transform->position.y,
                         transform->position.z,
                         meshComp->meshPath.c_str());
            std::fflush(stderr);
            s_once = true;
        }
    }

    static uint32_t s_diagFrame = 0;
    if (s_diagFrame < 8) {
        std::fprintf(stderr,
                     "[SkinnedMeshRenderSystem] frame=%u submitted=%u "
                     "meshFail=%u sceneItems=%zu\n",
                     s_diagFrame, submitted, skippedNoMesh,
                     scene.items().size());
        ++s_diagFrame;
    }
}

void registerSkinnedMeshRenderSystem()
{
    World::instance().registerSystem<SkinnedMeshRenderSystem>(
        SkinnedMeshRenderSystem::kPriority);
}

} // namespace ayt::entity
