// AYSkinnedMeshRenderSystem.cpp — Phase 1 E-04 implementation.
//
// Submits draws for entities with MeshComponent::skinned == true
// AND a SkeletonComponent. Coexists with RenderSystem via
// RendererSubSystem::setSceneBuilder's append-to-chain behavior.
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
#include <AYRenderer/RendererSubSystem.h>
#include <AYEntity/World.h>
#include <AYMath/MathTransform.h>
#include <AYMath/MathUtils.h>
#include <AYResource/AssetPath.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/assetsDefs/IMesh.h>
#include <AYIO/Env.h>

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

ayt::math::FVector3 indexedRangeCenter(
    const ayt::resource::IMesh& mesh,
    const ayt::resource::IMesh::Submesh& submesh)
{
    if (!mesh.hasAttribute(ayt::resource::MeshAttribute::Position)
        || mesh.getVertexData() == nullptr || mesh.getIndexData() == nullptr
        || mesh.getVertexStride() == 0 || submesh.indexCount == 0) {
        return mesh.hasBounds() ? mesh.getBounds().center
                                : ayt::math::FVector3(0.0f, 0.0f, 0.0f);
    }

    const uint64_t rangeEnd = std::min<uint64_t>(
        static_cast<uint64_t>(submesh.indexOffset) + submesh.indexCount,
        mesh.getIndexCount());
    const auto positionInfo =
        mesh.getAttributeInfo(ayt::resource::MeshAttribute::Position);
    if (positionInfo.count < 3 || rangeEnd <= submesh.indexOffset) {
        return mesh.hasBounds() ? mesh.getBounds().center
                                : ayt::math::FVector3(0.0f, 0.0f, 0.0f);
    }

    double sumX = 0.0;
    double sumY = 0.0;
    double sumZ = 0.0;
    uint64_t validCount = 0;
    for (uint64_t cursor = submesh.indexOffset; cursor < rangeEnd; ++cursor) {
        const uint32_t vertexIndex = mesh.getIndexData()[cursor];
        if (vertexIndex >= mesh.getVertexCount()) {
            continue;
        }
        float position[3]{};
        const uint8_t* source = mesh.getVertexData()
            + static_cast<size_t>(vertexIndex) * mesh.getVertexStride()
            + positionInfo.offset;
        std::memcpy(position, source, sizeof(position));
        sumX += position[0];
        sumY += position[1];
        sumZ += position[2];
        ++validCount;
    }
    if (validCount == 0) {
        return mesh.hasBounds() ? mesh.getBounds().center
                                : ayt::math::FVector3(0.0f, 0.0f, 0.0f);
    }
    const double inverseCount = 1.0 / static_cast<double>(validCount);
    return ayt::math::FVector3(
        static_cast<float>(sumX * inverseCount),
        static_cast<float>(sumY * inverseCount),
        static_cast<float>(sumZ * inverseCount));
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

const char* kSkinnedLitFragmentSc = R"(
$input v_normal, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    vec3 n = normalize(v_normal);
    vec3 lightDir = normalize(vec3(0.35, -0.85, -0.4));
    float ndotl = max(dot(n, -lightDir), 0.0);
    const float ambient = 0.22;
    const float diffuse = 0.78 * ndotl;
    vec3 baseColor = vec3(0.92, 0.78, 0.55);
    gl_FragColor = vec4(baseColor * (ambient + diffuse), 1.0);
}
)";

const char* kRigidLitVaryingDef = R"(
vec3 v_normal    : NORMAL    = vec3(0.0, 0.0, 1.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec2 a_texcoord0 : TEXCOORD0;
)";

const char* kRigidLitVertexSc = R"(
$input a_position, a_normal, a_texcoord0
$output v_normal, v_texcoord0
#include <bgfx_shader.sh>
void main()
{
    v_texcoord0 = a_texcoord0;
    v_normal = mul(u_model[0], vec4(a_normal, 0.0)).xyz;
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
)";

} // namespace

void SkinnedMeshRenderSystem::onStart()
{
    _materialCache.clear();
    _meshSubmissionCache.clear();

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

    rss->setSceneBuilder([this](ayt::render::RenderScene& scene) {
        buildSkinnedScene(scene);
    });

    _started = true;
    std::fprintf(stderr,
                 "[SkinnedMeshRenderSystem] scene-builder registered (chain mode)\n");
}

void SkinnedMeshRenderSystem::onUpdate(float /*dt*/)
{
}

ayt::render::MaterialHandle SkinnedMeshRenderSystem::loadMaterialCached(
    ayt::render::Renderer& renderer, const std::string& path)
{
    if (path.empty()) {
        return {};
    }
    const MaterialKey key{path};
    const auto found = _materialCache.find(key);
    if (found != _materialCache.end()) {
        return found->second;
    }

    const ayt::render::MaterialHandle material = renderer.loadMaterial(path);
    if (material.isValid()) {
        _materialCache.emplace(key, material);
    }
    return material;
}

SkinnedMeshRenderSystem::CachedMeshSubmission*
SkinnedMeshRenderSystem::loadMeshSubmissionCached(
    ayt::render::Renderer& renderer, const std::string& meshPath)
{
    const auto found = _meshSubmissionCache.find(meshPath);
    if (found != _meshSubmissionCache.end()) {
        return &found->second;
    }

    CachedMeshSubmission cached;
    cached.mesh = renderer.loadMesh(meshPath);
    if (!cached.mesh.isValid()) {
        return nullptr;
    }

    const std::shared_ptr<ayt::resource::IMesh> sourceMesh =
        ayt::resource::ResourceManager::instance()
            .load<ayt::resource::IMesh>(meshPath);
    if (sourceMesh == nullptr) {
        return nullptr;
    }

    const uint32_t submeshCount = sourceMesh->getSubmeshCount();
    const uint32_t slotCount = sourceMesh->getMaterialSlotCount();
    const ayt::resource::IMesh::Submesh* submeshes = sourceMesh->getSubmeshes();
    const uint32_t paletteCount = sourceMesh->getSkinPaletteCount();
    const ayt::resource::SkinPalette* palettes = sourceMesh->getSkinPalettes();
    const uint32_t paletteJointCount = sourceMesh->getSkinPaletteJointCount();
    const uint32_t* paletteJoints = sourceMesh->getSkinPaletteJoints();
    if (paletteCount != 0u && paletteCount != submeshCount) {
        std::fprintf(stderr,
                     "[SkinnedMeshRenderSystem] mesh '%s' has %u palettes for %u submeshes\n",
                     meshPath.c_str(), paletteCount, submeshCount);
        return nullptr;
    }

    bool legacyOrdinalSlots = submeshCount > 1
                           && submeshCount == slotCount
                           && submeshes != nullptr;
    for (uint32_t i = 0; legacyOrdinalSlots && i < submeshCount; ++i) {
        legacyOrdinalSlots = submeshes[i].materialIndex == 0;
    }
    if (legacyOrdinalSlots) {
        static bool s_legacySlotLog = false;
        if (!s_legacySlotLog) {
            std::fprintf(stderr,
                         "[SkinnedMeshRenderSystem] legacy submesh "
                         "material indices detected; using slot order\n");
            s_legacySlotLog = true;
        }
    }

    cached.submeshes.reserve(submeshCount);
    for (uint32_t i = 0; submeshes != nullptr && i < submeshCount; ++i) {
        const ayt::resource::IMesh::Submesh& submesh = submeshes[i];
        if (submesh.indexCount == 0) {
            continue;
        }

        CachedSubmeshSubmission submission;
        submission.firstIndex = submesh.indexOffset;
        submission.indexCount = submesh.indexCount;
        submission.sourceMaterialIndex = submesh.materialIndex;
        submission.localCenter = indexedRangeCenter(*sourceMesh, submesh);
        if (palettes != nullptr && i < paletteCount) {
            const ayt::resource::SkinPalette& palette = palettes[i];
            const uint64_t paletteEnd = static_cast<uint64_t>(palette.jointOffset)
                                      + palette.jointCount;
            if (paletteEnd > paletteJointCount
                || (palette.jointCount > 0u && paletteJoints == nullptr)) {
                std::fprintf(stderr,
                             "[SkinnedMeshRenderSystem] mesh '%s' palette %u is invalid\n",
                             meshPath.c_str(), i);
                return nullptr;
            }
            if (palette.jointCount > 0u) {
                submission.bonePalette.assign(paletteJoints + palette.jointOffset,
                                              paletteJoints + paletteEnd);
            }
        }
        const uint32_t slotIndex = legacyOrdinalSlots ? i : submesh.materialIndex;
        if (slotIndex < slotCount) {
            const char* slot = sourceMesh->getMaterialSlot(slotIndex);
            if (slot != nullptr && slot[0] != '\0') {
                const std::string materialPath =
                    ayt::resource::resolveAssetPath(meshPath, slot);
                submission.materialPath = materialPath;
                submission.material = loadMaterialCached(renderer, materialPath);
                if (!submission.material.isValid()) {
                    static uint32_t s_slotFailLog = 0;
                    if (s_slotFailLog < 5) {
                        std::fprintf(stderr,
                                     "[SkinnedMeshRenderSystem] loadMaterial "
                                     "slot %u ('%s') failed; using fallback\n",
                                     slotIndex, materialPath.c_str());
                        ++s_slotFailLog;
                    }
                }
            }
        }
        cached.submeshes.push_back(submission);
    }

    if (_diagnosticLog) {
        uint64_t coveredIndices = 0;
        uint32_t invalidRanges = 0;
        uint32_t cachedOrdinal = 0;
        for (uint32_t i = 0; i < submeshCount; ++i) {
            const auto& submesh = submeshes[i];
            const uint64_t end = static_cast<uint64_t>(submesh.indexOffset)
                               + static_cast<uint64_t>(submesh.indexCount);
            if (submesh.indexCount == 0 || end > sourceMesh->getIndexCount()) {
                ++invalidRanges;
            }
            coveredIndices += submesh.indexCount;
            const uint32_t slotIndex = legacyOrdinalSlots ? i : submesh.materialIndex;
            const char* slot = slotIndex < slotCount
                             ? sourceMesh->getMaterialSlot(slotIndex) : nullptr;
            const bool materialLoaded = submesh.indexCount != 0
                && cachedOrdinal < cached.submeshes.size()
                && cached.submeshes[cachedOrdinal++].material.isValid();
            std::fprintf(stderr,
                         "[SkinnedMeshDiagnostic] mesh=%s submesh=%u "
                         "first=%u count=%u materialIndex=%u slot=%u "
                         "material=%s loaded=%d range=%s\n",
                         meshPath.c_str(), i, submesh.indexOffset,
                         submesh.indexCount, submesh.materialIndex, slotIndex,
                         slot != nullptr ? slot : "<invalid-slot>",
                         materialLoaded ? 1 : 0,
                         (submesh.indexCount != 0 && end <= sourceMesh->getIndexCount())
                             ? "ok" : "INVALID");
        }
        std::fprintf(stderr,
                     "[SkinnedMeshDiagnostic] mesh=%s vertices=%u indices=%u "
                     "submeshes=%u slots=%u coveredIndices=%llu invalidRanges=%u "
                     "skin=%d bounds=%d\n",
                     meshPath.c_str(), sourceMesh->getVertexCount(),
                     sourceMesh->getIndexCount(), submeshCount, slotCount,
                     static_cast<unsigned long long>(coveredIndices), invalidRanges,
                     sourceMesh->hasSkinWeights() ? 1 : 0,
                     sourceMesh->hasBounds() ? 1 : 0);
    }

    auto [inserted, unused] = _meshSubmissionCache.emplace(meshPath, std::move(cached));
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
    const ayt::math::FVector3 cameraPosition = renderer.mainCameraPosition();

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
            loadMeshSubmissionCached(renderer, meshComp->meshPath);
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
                loadMaterialCached(renderer, meshComp->materialPath);
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
            ayt::math::Transform::getMatrix(
                                            transform->interpolatedPosition(interpolationAlpha),
                                            transform->interpolatedRotation(interpolationAlpha),
                                            transform->scale);

        bool submittedSubmesh = false;
        uint32_t submeshOrdinal = 0;
        for (const CachedSubmeshSubmission& submesh : meshSubmission->submeshes) {
            const ayt::render::MaterialHandle submeshMat =
                _diagnosticSolid
                    ? rigidIt->second
                    : (submesh.material.isValid() ? submesh.material : fallbackMat);
            ayt::render::DrawItem item;
            item.mesh = meshSubmission->mesh;
            item.material = submeshMat;
            item.firstIndex = submesh.firstIndex;
            item.indexCount = submesh.indexCount;
            item.world = worldM;
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
            scene.add(meshSubmission->mesh, fallbackMat, worldM);
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
