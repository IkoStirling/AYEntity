// AYSkinnedMeshRenderSystem.cpp — Phase 1 E-04 implementation.
//
// Submits draws for entities with `MeshComponent::skinned == true`
// AND a SkeletonComponent. The renderer's existing RenderSystem skips
// those entities (see AYRenderSystem.cpp early-out); this system owns
// their scene-builder slot.
//
// Phase 1 demo assumption: skinned entities all share one SkinnedLit
// material compiled from an inline Phoskia source string. Phase 2
// will resolve a `.aymat` path the same way RenderSystem does.

#include "AYSkinnedMeshRenderSystem.h"

#include <components/AYMeshComponent.h>
#include <components/AYSkeletonComponent.h>

#include <AYEntity.h>
#include <AYEntityModule.h>
#include <AYRenderScene.h>
#include <AYRenderer.h>
#include <AYRendererSubSystem.h>
#include <AYWorld.h>
#include <AYMathTransform.h>

#include <cstdio>
#include <cstring>

namespace ayt::entity
{

namespace
{

// Inline Phoskia source for the SkinnedLit material. Mirrors
// AYShader/unittest/golden/skinned_lit.phoskia but uses a fixed
// light direction + base color so we don't need a texture sampler.
// The `Skeleton` UBO is mandatory — Phoskia's BGFX backend emits a
// `layout(std140, binding = 0) uniform Skeleton { mat4 bones[128]; }`
// declaration that the renderer binds via setUniformBlock each draw.
const char* kSkinnedLitPhoskia = R"(
uniformblock Skeleton {
    mat4 bones[128]
}
material SkinnedLit {
    property baseColor = vec4(1.0, 1.0, 1.0, 1.0)
    uniform vec3 lightDir
    uniform vec3 lightColor

    vertex {
        in pos    : position
        in nrm    : normal
        in uv     : texcoord
        in boneId : boneindices
        in boneWt : boneweights

        out vWorldNormal : normal = vec3(0.0, 0.0, 1.0)
        out vUv          : texcoord = vec2(0.0, 0.0)

        let skinned = skinningMatrix(boneId, boneWt, Skeleton.bones, vec4(pos, 1.0))
        return modelViewProjection * skinned
    }
    fragment {
        in vWorldNormal : normal
        in vUv          : texcoord
        let N = normalize(vWorldNormal)
        let L = normalize(-lightDir)
        let lambert = max(dot(N, L), 0.0)
        return vec4(baseColor.rgb * lightColor * lambert, baseColor.a)
    }
}
)";

} // namespace

void SkinnedMeshRenderSystem::onStart()
{
    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) {
        std::fprintf(stderr,
                     "[SkinnedMeshRenderSystem] RendererSubSystem not registered; "
                     "skinned draws will not be submitted.\n");
        return;
    }

    // Phase 1 SC-01: append our scene-builder to the chain so we
    // coexist with RenderSystem (priority 500 each; chain order is
    // registration order = RenderSystem was registered first via
    // registerRenderSystem() in AYEntityModule.cpp).
    rss->setSceneBuilder([this](ayt::render::RenderScene& scene) {
        buildSkinnedScene(scene);
    });

    _started = true;
    std::fprintf(stderr,
                 "[SkinnedMeshRenderSystem] scene-builder registered (chain mode)\n");
}

void SkinnedMeshRenderSystem::onUpdate(float /*dt*/)
{
    // Animation is owned by AnimationSystem. We only consume the
    // skin matrices it produced. No per-frame work here.
}

void SkinnedMeshRenderSystem::buildSkinnedScene(ayt::render::RenderScene& scene)
{
    if (!_started) return;

    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) return;
    ayt::render::Renderer& renderer = rss->renderer();

    World& world = World::instance();
    for (Entity* e : world.query<Transform, MeshComponent, SkeletonComponent>()) {
        if (e == nullptr) continue;
        Transform*       transform = e->getComponent<Transform>();
        MeshComponent*   meshComp  = e->getComponent<MeshComponent>();
        SkeletonComponent* skel    = e->getComponent<SkeletonComponent>();
        if (transform == nullptr || meshComp == nullptr || skel == nullptr) continue;

        // Route rule: only skinned entities pass through here. The
        // non-skinned query (Transform + MeshComponent) is handled by
        // RenderSystem.
        if (!meshComp->skinned) continue;
        if (!skel->isValid()) continue;

        // Resolve mesh (cached; second call returns the same handle).
        const ayt::render::MeshHandle meshHandle =
            renderer.loadMesh(meshComp->meshPath);
        if (!meshHandle.isValid()) {
            std::fprintf(stderr,
                         "[SkinnedMeshRenderSystem] loadMesh('%s') failed\n",
                         meshComp->meshPath.c_str());
            continue;
        }

        // Resolve or create the SkinnedLit material. We use one
        // fixed Phoskia source string + a stable cache key so the
        // shader pool only compiles it once.
        constexpr const char* kSkinnedLitCacheKey = "AYEntity_SkinnedLit_v1";
        const MaterialKey matKey{ kSkinnedLitCacheKey };
        auto matIt = _materialCache.find(matKey);
        if (matIt == _materialCache.end()) {
            const ayt::render::MaterialHandle h =
                renderer.createMaterialFromPhoskia(kSkinnedLitPhoskia,
                                                  kSkinnedLitCacheKey);
            if (!h.isValid()) {
                std::fprintf(stderr,
                             "[SkinnedMeshRenderSystem] createMaterialFromPhoskia"
                             " failed; check shaderc availability\n");
                continue;
            }
            matIt = _materialCache.emplace(matKey, h).first;
        }

        // World matrix from entity Transform.
        const ayt::math::Float4x4 world =
            ayt::math::Transform::getMatrix(transform->position,
                                            transform->rotation,
                                            transform->scale);

        // Submit with per-frame bone matrices. ForwardOpaquePass
        // lazily resolves the Skeleton UBO binding the first time
        // it sees boneMatrices != nullptr.
        scene.add(meshHandle, matIt->second, world,
                  skel->skinMatrices, skel->jointCount);
    }
}

void registerSkinnedMeshRenderSystem()
{
    static bool registered = false;
    if (registered) return;
    registered = true;
    World::instance().registerSystem<SkinnedMeshRenderSystem>(
        SkinnedMeshRenderSystem::kPriority);
}

} // namespace ayt::entity