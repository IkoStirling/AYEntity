// AYSkinnedMeshRenderSystem.cpp — Phase 1 E-04 implementation.
//
// Submits draws for entities with MeshComponent::skinned == true
// AND a SkeletonComponent. Coexists with RenderSystem via
// RendererSubSystem::setSceneBuilder's append-to-chain behavior.
//
// Deferred GBufferFill has no bone palette. Until a skinned GBuffer
// path exists, every skinned entity is submitted as rigid bind-pose
// (raw mesh verts, no boneMatrices) so characters stay visible.

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

#include <cstdio>

namespace ayt::entity
{

namespace {

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

void SkinnedMeshRenderSystem::buildSkinnedScene(ayt::render::RenderScene& scene)
{
    if (!_started) return;

    ayt::render::RendererSubSystem* rss =
        ayt::render::RendererSubSystem::findRegistered();
    if (rss == nullptr) return;
    ayt::render::Renderer& renderer = rss->renderer();

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

        const ayt::render::MeshHandle meshHandle =
            renderer.loadMesh(meshComp->meshPath);
        if (!meshHandle.isValid()) {
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

        // Prefer the cooked .aymat (textures → GBuffer albedoMap via alias).
        // Fall back to RigidLit with an explicit tint so Deferred never
        // fills solid white when the shader file is missing.
        ayt::render::MaterialHandle drawMat = rigidIt->second;
        if (!meshComp->materialPath.empty()) {
            const ayt::render::MaterialHandle cooked =
                renderer.loadMaterial(meshComp->materialPath);
            if (cooked.isValid()) {
                drawMat = cooked;
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

        const ayt::math::Float4x4 worldM =
            ayt::math::Transform::getMatrix(transform->position,
                                            transform->rotation,
                                            transform->scale);
        scene.add(meshHandle, drawMat, worldM);
        ++submitted;

        static bool s_once = false;
        if (!s_once) {
            std::fprintf(stderr,
                         "[SkinnedMeshRenderSystem] bind-pose submit "
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
