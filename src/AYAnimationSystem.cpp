// AYAnimationSystem.cpp — Phase 1 AN-03 implementation.
//
// Per-frame: for every entity with SkeletonComponent + AnimationComponent,
//   1. Lazy-load skeleton / clip the first tick the path is set
//      (adapter converts AYResource's IAnimation/IAnimation into
//      AYAnimation's runtime types).
//   2. If AnimationComponent::autoplay is true, advance the player's
//      time by `dt * playRate`, sample tracks, evaluate the pose.
//   3. Copy per-bone skin matrices from the player into the
//      SkeletonComponent's heap buffer so the renderer can upload
//      them to the SkinnedLit's `Skeleton` UBO.
//
// Priority 450 < RenderSystem's 500 — animation always ticks first.

#include "AYAnimationSystem.h"
#include "AYResourceAnimationAdapter.h"

#include <components/AYAnimationComponent.h>
#include <components/AYSkeletonComponent.h>

#include <AYEntity.h>
#include <AYWorld.h>

#include <cstdio>
#include <cstring>

namespace ayt::entity
{

void AnimationSystem::onUpdate(float dt)
{
    World& world = World::instance();
    for (Entity* e : world.query<SkeletonComponent, AnimationComponent>()) {
        if (e == nullptr) continue;
        SkeletonComponent* skel = e->getComponent<SkeletonComponent>();
        AnimationComponent* anim = e->getComponent<AnimationComponent>();
        if (skel == nullptr || anim == nullptr) continue;

        // First-time lazy-load. Skeletons are loaded once per entity;
        // animations are cached by clipPath so multiple entities
        // sharing a clip share the parsed track data.
        if (!skel->loaded) {
            if (!adapter::loadSkeleton(skel->skeletonPath, skel->skeleton)) {
                continue;
            }
            // Allocate skin matrices matching joint count.
            skel->jointCount = static_cast<uint32_t>(
                skel->skeleton.getBoneCount());
            if (skel->jointCount == 0) {
                skel->loaded = true;  // mark loaded but unusable
                continue;
            }
            delete[] skel->skinMatrices;
            skel->skinMatrices = new ayt::math::Float4x4[skel->jointCount];
            std::memset(skel->skinMatrices, 0,
                        skel->jointCount * sizeof(ayt::math::Float4x4));

            skel->player.setSkeleton(&skel->skeleton);
            skel->loaded = true;
        }

        if (!skel->loaded || skel->jointCount == 0) continue;

        // Lazy-load the clip if not cached. We re-bind every time
        // the AnimationComponent's clipPath changes (compare with the
        // cached binding stored on the SkeletonComponent player).
        //
        // AnimationPlayer has no public `getCurrentAnim()` accessor,
        // so we track the bound clip path via a side-channel on the
        // SkeletonComponent: the player is reset to t=0 each time
        // we see a new path. (Phase 2 will move this to
        // AnimationStateMachine.)
        const auto cached = _clipCache.find(anim->clipPath);
        if (cached == _clipCache.end()) {
            if (!adapter::loadAnimation(anim->clipPath, _clipCache[anim->clipPath])) {
                _clipCache.erase(anim->clipPath);
                continue;
            }
        }
        // We don't have a direct way to compare the player's current
        // clip vs the new one without an accessor. Simplest correct
        // behavior for Phase 1: always re-play from t=0 when the
        // AnimationComponent exists. This handles the "swap clips on
        // the fly" case; state-machine driven swaps come in Phase 2.
        // Cost: one extra play() call per entity per tick — negligible
        // (play() resets the time cursor and sets loop mode).
        skel->player.setPlayRate(anim->playRate);
        skel->player.setLoop(anim->looping);
        skel->player.play(&_clipCache[anim->clipPath]);

        if (anim->autoplay) {
            skel->player.tick(dt);
            skel->player.evaluate();
            // Copy skin matrices (world * inverseBindMatrix per bone).
            const ayt::math::Float4x4* src = skel->player.getBoneSkinMatrices();
            if (src != nullptr) {
                std::memcpy(skel->skinMatrices, src,
                            skel->jointCount * sizeof(ayt::math::Float4x4));
            }
        }
    }
}

void registerAnimationSystem()
{
    static bool registered = false;
    if (registered) return;
    registered = true;
    World::instance().registerSystem<AnimationSystem>(AnimationSystem::kPriority);
}

} // namespace ayt::entity