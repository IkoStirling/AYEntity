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
#include <AYMathUtils.h>

#include <cstdio>
#include <cstring>

namespace ayt::entity
{

namespace
{

// Phase 1 demo: compile SkinnedLit from hand-authored bgfx .sc sources
// (golden skinned_lit.sc shape, bones[4] for the 4-bone test rig).
// Phoskia → shaderc is exercised elsewhere; this path keeps the demo
// stable while the skinned Phoskia backend matures.
const char* kSkinnedLitVaryingDef = R"(
vec3 v_normal    : NORMAL    = vec3(0.0, 0.0, 1.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec2 a_texcoord0 : TEXCOORD0;
vec4 a_indices   : BLENDINDICES;
vec4 a_weight    : BLENDWEIGHT;
)";

const char* kSkinnedLitVertexSc = R"(
$input a_position, a_normal, a_texcoord0, a_indices, a_weight
$output v_normal, v_texcoord0

#include <bgfx_shader.sh>

uniform mat4 bones[4];

void main()
{
    v_texcoord0 = a_texcoord0;

    // BLENDINDICES are normalized uint8 in bgfx (index / 255).
    ivec4 bi = ivec4(a_indices * 255.0 + 0.5);

    vec4 pos = vec4(a_position, 1.0);
    vec4 skinnedPos =
          a_weight.x * mul(bones[bi.x], pos)
        + a_weight.y * mul(bones[bi.y], pos)
        + a_weight.z * mul(bones[bi.z], pos)
        + a_weight.w * mul(bones[bi.w], pos);

    vec3 nrm = a_normal;
    vec3 skinnedNrm =
          a_weight.x * mul(bones[bi.x], vec4(nrm, 0.0)).xyz
        + a_weight.y * mul(bones[bi.y], vec4(nrm, 0.0)).xyz
        + a_weight.z * mul(bones[bi.z], vec4(nrm, 0.0)).xyz
        + a_weight.w * mul(bones[bi.w], vec4(nrm, 0.0)).xyz;

    v_normal = mul(u_model[0], vec4(skinnedNrm, 0.0)).xyz;
    gl_Position = mul(u_modelViewProj, skinnedPos);
}
)";

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
    uint32_t submitted = 0;
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
        if (!skel->isValid()) {
            static uint32_t s_invalidSkelLog = 0;
            if (s_invalidSkelLog < 3) {
                std::fprintf(stderr,
                             "[SkinnedMeshRenderSystem] skip: skeleton not loaded "
                             "(loaded=%d jointCount=%u)\n",
                             skel->loaded ? 1 : 0, skel->jointCount);
                std::fflush(stderr);
                ++s_invalidSkelLog;
            }
            continue;
        }

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
        constexpr const char* kSkinnedLitCacheKey = "AYEntity_SkinnedLit_bgfx_v11";
        const MaterialKey matKey{ kSkinnedLitCacheKey };
        auto matIt = _materialCache.find(matKey);
        if (matIt == _materialCache.end()) {
            std::fprintf(stderr,
                         "[SkinnedMeshRenderSystem] compiling SkinnedLit (bgfx .sc)...\n");
            std::fflush(stderr);
            const ayt::render::MaterialHandle h =
                renderer.createMaterialFromBgfxSc(kSkinnedLitVertexSc,
                                                  kSkinnedLitFragmentSc,
                                                  kSkinnedLitVaryingDef,
                                                  kSkinnedLitCacheKey);
            if (!h.isValid()) {
                std::fprintf(stderr,
                             "[SkinnedMeshRenderSystem] createMaterialFromBgfxSc"
                             " failed; check shaderc + bgfx include dirs\n");
                std::fflush(stderr);
                continue;
            }
            matIt = _materialCache.emplace(matKey, h).first;
        }

        // Static tilt so multiple faces are visible; animation comes from
        // GPU skin matrices (spine bone track), not entity bob.
        const ayt::math::Float4x4 tilt =
            ayt::math::rotate(ayt::math::FVector3(1.0f, 0.0f, 0.0f), 0.45f);
        const ayt::math::Float4x4 local =
            ayt::math::Transform::getMatrix(transform->position,
                                            transform->rotation,
                                            transform->scale);
        const ayt::math::Float4x4 world = tilt * local;

        scene.add(meshHandle, matIt->second, world,
                  skel->skinMatrices, skel->jointCount);
        ++submitted;
    }

    static uint32_t s_diagFrame = 0;
    if (s_diagFrame < 5) {
        std::fprintf(stderr,
                     "[SkinnedMeshRenderSystem] frame=%u submitted=%u sceneItems=%zu\n",
                     s_diagFrame, submitted, scene.items().size());
        ++s_diagFrame;
    }
}

void registerSkinnedMeshRenderSystem()
{
    // GL-01: idempotent across World::shutdown — see the matching
    // comment in AYAnimationSystem.cpp. The bootstrapModule() guard
    // is the only one we need.
    World::instance().registerSystem<SkinnedMeshRenderSystem>(
        SkinnedMeshRenderSystem::kPriority);
}

} // namespace ayt::entity