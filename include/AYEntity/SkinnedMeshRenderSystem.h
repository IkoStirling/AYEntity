#pragma once
// AYEntity/SkinnedMeshRenderSystem.h — Phase 1 E-04: scene-builder callback
// that submits skinned-mesh draws (entities with MeshComponent::skinned
// == true AND a SkeletonComponent). Coexists with RenderSystem via
// RendererSubSystem::setSceneBuilder's append-to-chain behavior.

#include <AYEntity/IEntity.h>
#include <AYMath/MathTypes.h>
#include <AYRenderer/RenderTypes.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::render
{
struct RenderScene;
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
    ayt::render::MaterialHandle loadMaterialCached(
        ayt::render::Renderer& renderer, const std::string& path);

    struct MaterialKey {
        std::string path;
        bool operator==(const MaterialKey& o) const { return path == o.path; }
    };
    struct MaterialKeyHash {
        size_t operator()(const MaterialKey& k) const noexcept {
            return std::hash<std::string>{}(k.path);
        }
    };

    // Per-system cache for generated fallback materials. Cooked .aymat
    // paths also live here so the per-frame path never re-enters Renderer.
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
        std::vector<CachedSubmeshSubmission> submeshes;
    };

    CachedMeshSubmission* loadMeshSubmissionCached(
        ayt::render::Renderer& renderer, const std::string& meshPath);

    std::unordered_map<std::string, CachedMeshSubmission> _meshSubmissionCache;

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
