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
// destructor (`include/components/AYSkeletonComponent.h:48-52`) `delete[]`s
// its `skinMatrices`; we don't need to do anything special here.

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

namespace ayt::entity
{

Entity* spawnCharacterFromPaths(const std::string& meshPath,
                                const std::string& materialPath,
                                const std::string& skeletonPath,
                                const std::string& animationPath)
{
    // Matches `Entity::create` / `Entity::destroy` convention used by
    // existing unit tests (`unittest/EntityTest.cpp`) and by the
    // editor + tools callers. Internally Entity::create delegates to
    // World::instance().createEntity().
    Entity* entity = Entity::create();
    if (entity == nullptr) {
        return nullptr;
    }

    entity->addComponent<Transform>();

    auto* mesh = entity->addComponent<MeshComponent>();
    mesh->meshPath     = meshPath;
    mesh->materialPath = materialPath;
    mesh->skinned      = true;

    auto* skel = entity->addComponent<SkeletonComponent>();
    skel->skeletonPath = skeletonPath;

    auto* anim = entity->addComponent<AnimationComponent>();
    anim->clipPath = animationPath;
    // AnimationComponent ctor already sets autoplay=true, looping=true,
    // playRate=1.0f (see components/AYAnimationComponent.h:28-32). We
    // assign again here so future component-default tweaks don't leak
    // into this codepath silently.
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
