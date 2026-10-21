#pragma once
// AYEntity/SkinnedMeshRenderSystem.h — Phase 1 E-04: scene-builder callback
// that submits skinned-mesh draws (entities with MeshComponent::skinned
// == true AND a SkeletonComponent). Coexists with RenderSystem in the
// active World's owner-scoped scene-builder group.

#include <AYEntity/IEntity.h>
#include <AYMath/MathTypes.h>
#include <AYRenderer/RenderTypes.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ayt::render
{
struct RenderScene;
struct MeshSubmissionInfo;
class Renderer;
}

namespace ayt::entity
{

class SkinnedMeshRenderSystem : public ISystem {
public:
    const char* getName() const override { return "SkinnedMeshRenderSystem"; }
    void onStart() override;
    void onUpdate(float dt) override;

    static constexpr int kPriority = 500;

private:
    void buildSkinnedScene(ayt::render::RenderScene& scene);
    struct ExtractionResources;
    ayt::render::MaterialHandle loadMaterialCached(
        ayt::render::Renderer& renderer, const std::string& path, ExtractionResources& extraction);

    struct MaterialKey {
        std::string path;
        bool operator==(const MaterialKey& o) const { return path == o.path; }
    };
    struct MaterialKeyHash {
        size_t operator()(const MaterialKey& k) const noexcept {
            return std::hash<std::string>{}(k.path);
        }
    };

    // Generated fallback instances only. Path materials use Renderer's current
    // path cache; context identity prevents reuse after device reconstruction.
    std::unordered_map<MaterialKey, ayt::render::MaterialHandle, MaterialKeyHash> _materialCache;

    struct CachedSubmeshSubmission {
        ayt::render::MaterialHandle material;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t sourceMaterialIndex = 0;
        std::string materialPath;
        // Bind-pose center of this index range. TransparentPass sorts whole
        // DrawItems, so every imported submesh needs its own spatial key;
        // using only the entity origin leaves all character layers tied.
        ayt::math::FVector3 localCenter{0.0f, 0.0f, 0.0f};
        // Draw-local slot -> SkeletonComponent::skinMatrices index.
        std::vector<uint32_t> bonePalette;
    };
    struct CachedMeshSubmission {
        ayt::render::MeshHandle mesh;
        std::shared_ptr<const ayt::render::MeshSubmissionInfo> snapshot;
        uint64_t extraction = 0;
        std::vector<CachedSubmeshSubmission> submeshes;
    };

    CachedMeshSubmission* loadMeshSubmissionCached(
        ayt::render::Renderer& renderer, const std::string& meshPath, ExtractionResources& extraction);

    // One build invocation (including one camera view), never a frame/global
    // revision heuristic. Negative lookups retry on the next extraction.
    struct ExtractionResources {
        std::unordered_set<std::string> failedMaterials;
        std::unordered_set<std::string> failedMeshes;
    };

    std::unordered_map<std::string, CachedMeshSubmission> _meshSubmissionCache;
    struct CachedPathMaterial {ayt::render::MaterialHandle handle;uint64_t extraction=0;};
    std::unordered_map<std::string,CachedPathMaterial> _pathMaterialCache;
    std::vector<std::string> _assetRoots;
    uint64_t _resourceContextId = 0;
    uint64_t _extractionSerial = 0;

    // P2.1 visibility diagnostics. These are intentionally process-local
    // switches so the normal material pipeline remains unchanged. Set
    // AY_SKINNED_DIAGNOSTIC=log, solid, or solid-doublesided before launch.
    bool _diagnosticLog = false;
    bool _diagnosticSolid = false;
    bool _diagnosticDoubleSided = false;

    bool _started = false;
};

void registerSkinnedMeshRenderSystem();

} // namespace ayt::entity
