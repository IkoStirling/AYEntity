#pragma once
// AYSkinnedMeshRenderSystem.h — Phase 1 E-04: scene-builder callback
// that submits skinned-mesh draws (entities with MeshComponent::skinned
// == true AND a SkeletonComponent). Coexists with RenderSystem via
// RendererSubSystem::setSceneBuilder's append-to-chain behavior.

#include <IAYEntity.h>

#include <cstdint>
#include <string>
#include <unordered_map>

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

    struct MaterialKey {
        std::string path;
        bool operator==(const MaterialKey& o) const { return path == o.path; }
    };
    struct MaterialKeyHash {
        size_t operator()(const MaterialKey& k) const noexcept {
            return std::hash<std::string>{}(k.path);
        }
    };

    // Per-system cache of (path → material handle). Keeps the
    // SkinnedLit material loaded once per unique cacheKey (which
    // for the Phase 1 demo is a constant ".phoskia" path).
    std::unordered_map<MaterialKey, ayt::render::MaterialHandle, MaterialKeyHash> _materialCache;

    bool _started = false;
};

void registerSkinnedMeshRenderSystem();

} // namespace ayt::entity