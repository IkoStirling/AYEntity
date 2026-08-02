// AYCharacterEntity.cpp — Phase 1 ED-02 implementation.
//
// Lifts the body of SuzanneSkinnedDemo::spawnSkinnedEntity
// (`AYRenderer/demo/SuzanneSkinnedDemo.cpp:408-435`) into the AYEntity
// library so the editor + future tools can reuse it. The original demo
// site is preserved unchanged; this is a copy, by design — keeping it
// in sync lets each tool control its own AssetPaths-resolution policy
// without changing the demo's hard-coded paths.
//
// Behavior parity with the demo:
//   * Identity TRS (callers can mutate via `entity->getComponent<Transform>()`)
//   * MeshComponent.skinMatrix pipeline routing via `skinned = true`
//   * AnimationComponent defaults: autoplay, looping, playRate=1.0f
//
// Destroy path uses `World::destroyEntity` which recycles the EntityHandle
// back into the pool and destructs components. The SkeletonComponent
// destructor (`include/components/AYSkeletonComponent.h`) `delete[]`s
// its `skinMatrices`; `player` is a unique_ptr + AnimationPlayerDeleter.

#include "AYCharacterEntity.h"

#include "AYEntity.h"                  // umbrella: instantiates
                                      //   Entity::addComponent<T>()
                                      //   for every T included below.
#include "include/AYEntityImpl.h"      // Entity::create / Entity::destroy
#include "include/AYWorld.h"
#include "include/components/AYAnimationComponent.h"
#include "include/components/AYMeshComponent.h"
#include "include/components/AYSkeletonComponent.h"
#include "include/components/AYTransformComponent.h"

#include <cstdio>

namespace ayt::entity
{

Entity* spawnCharacterFromPaths(const std::string& meshPath,
                                const std::string& materialPath,
                                const std::string& skeletonPath,
                                const std::string& animationPath)
{
    Entity* entity = Entity::create();
    if (entity == nullptr) {
        return nullptr;
    }

    entity->addComponent<Transform>();

    auto* mesh = entity->addComponent<MeshComponent>();
    if (mesh == nullptr) {
        Entity::destroy(entity);
        return nullptr;
    }
    mesh->meshPath     = meshPath;
    mesh->materialPath = materialPath;
    // Always route through SkinnedMeshRenderSystem. Bind-pose with an
    // empty materialPath used to flip skinned=false → RenderSystem, which
    // then skipped the draw (no .aymat) and the character vanished while
    // Transparent glass still drew. SkinnedMesh owns its own lit material;
    // jointCount > bones[128] falls back to rigid bind-pose there.
    mesh->skinned      = true;

    auto* skel = entity->addComponent<SkeletonComponent>();
    // Stale .obj with an older SkeletonComponent layout (embedded
    // AnimationPlayer / wrong unique_ptr) allocates the wrong size;
    // the out-of-line ctor then writes layoutMagic at the new offset
    // and subsequent string assigns AV (write @ 0). Refuse early.
    if (skel == nullptr
        || skel->layoutMagic != SkeletonComponent::kLayoutMagic) {
        std::fprintf(stderr,
                     "[spawnCharacter] SkeletonComponent layout mismatch "
                     "(magic=0x%08X expected=0x%08X sizeof=%zu) — "
                     "Clean+rebuild AYEntity\n",
                     skel ? skel->layoutMagic : 0u,
                     SkeletonComponent::kLayoutMagic,
                     sizeof(SkeletonComponent));
        std::fflush(stderr);
        Entity::destroy(entity);
        return nullptr;
    }
    skel->skeletonPath = skeletonPath;

    auto* anim = entity->addComponent<AnimationComponent>();
    if (anim == nullptr) {
        Entity::destroy(entity);
        return nullptr;
    }
    anim->clipPath = animationPath;
    anim->autoplay = true;
    anim->looping  = true;
    anim->playRate = 1.0f;

    return entity;
}

void destroyCharacter(Entity* entity)
{
    if (entity == nullptr) {
        return;
    }
    Entity::destroy(entity);
}

} // namespace ayt::entity
