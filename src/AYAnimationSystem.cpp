// AYAnimationSystem.cpp — Phase 1 AN-03 implementation.
//
// Per-frame: for every entity with SkeletonComponent + AnimationComponent,
//   1. Lazy-load skeleton / clip the first tick the path is set
//      (ResourceManager::load<ISkeleton/IAnimation>).
//   2. If AnimationComponent::autoplay is true, advance the player's
//      time by `dt * playRate`, sample tracks, evaluate the pose.
//   3. Copy per-bone skin matrices from the player into the
//      SkeletonComponent's heap buffer so the renderer can upload
//      them to the SkinnedLit's `Skeleton` UBO.
//
// Priority 450 < RenderSystem's 500 — animation always ticks first.
//
// P0 (2026-07-26): clip cache holds shared_ptr<IAnimation> (instead of
// copying the now-deleted ayt::anim::Animation type). Adapter deleted —
// AnimationPlayer consumes ISkeleton/IAnimation directly.

#include "AYAnimationSystem.h"

#include <components/AYAnimationComponent.h>
#include <components/AYSkeletonComponent.h>

#include <AYEntity.h>
#include <AYWorld.h>
#include <IAYSkeleton.h>
#include <AYResourceManager.h>

// Phase 1.5 — AnimNotify bridge: drain the player's per-frame fired
// markers and emit each as AnimNotifyEvent on the engine EventBus.
#include <ayanimation/AnimNotifyEvent.h>
#include <ayanimation/AnimationPlayer.h>
#include <ayevent/EventBus.h>

#include <cstdio>
#include <cstring>

namespace ayt::entity
{

namespace
{

float skinMatrixTranslationY(const ayt::math::Float4x4& m)
{
    return m.row[1].w;
}

} // namespace

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
            auto skelRes = ayt::resource::ResourceManager::instance()
                              .load<ayt::resource::ISkeleton>(skel->skeletonPath);
            if (!skelRes || skelRes->getBoneCount() == 0) {
                std::fprintf(stderr,
                             "[AnimationSystem] loadSkeleton('%s') failed\n",
                             skel->skeletonPath.c_str());
                continue;
            }
            // Copy from the ISkeleton resource into the component's
            // concrete Skeleton. setBoneCount resizes the parallel
            // arrays; setBone fills each entry (and re-registers the
            // name map + root indices).
            const size_t n = skelRes->getBoneCount();
            skel->skeleton.setBoneCount(n);
            for (size_t i = 0; i < n; ++i) {
                skel->skeleton.setBone(i, skelRes->getBones()[i]);
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
            for (uint32_t i = 0; i < skel->jointCount; ++i) {
                skel->skinMatrices[i] = ayt::math::Float4x4::identity();
            }

            skel->player.setSkeleton(&skel->skeleton);
            skel->loaded = true;
        }

        if (!skel->loaded || skel->jointCount == 0) continue;

        // Lazy-load the clip if not cached.
        if (_clipCache.find(anim->clipPath) == _clipCache.end()) {
            auto animRes = ayt::resource::ResourceManager::instance()
                              .load<ayt::resource::IAnimation>(anim->clipPath);
            if (!animRes) {
                std::fprintf(stderr,
                             "[AnimationSystem] loadAnimation('%s') failed\n",
                             anim->clipPath.c_str());
                continue;
            }
            _clipCache.emplace(anim->clipPath, animRes);
            std::fprintf(stderr,
                         "[AnimationSystem] loadAnimation ok '%s' tracks=%u duration=%.2fs\n",
                         anim->clipPath.c_str(),
                         animRes->getTrackCount(),
                         animRes->getDuration());
            std::fflush(stderr);
        }

        skel->player.setPlayRate(anim->playRate);
        skel->player.setLoop(anim->looping);
        // Phase 1.2 (P1.2) — Additive Layer 1 weight passthrough. The
        // AnimationPlayer's setter saturates to [0, 1] so a typo on the
        // engine side cannot NaN a quaternion. Default 1.0f on both sides
        // keeps pre-P1.2 clips (no Additive tracks) bit-identical.
        skel->player.setAdditiveWeight(anim->additiveWeight);

        const std::string& boundClip = _entityBoundClip[e];
        if (boundClip != anim->clipPath) {
            _entityBoundClip[e] = anim->clipPath;
            skel->player.play(_clipCache[anim->clipPath].get());
        }

        if (anim->autoplay) {
            skel->player.tick(dt);
        }

        if (skel->player.isValid()) {
            skel->player.evaluate();
            const ayt::math::Float4x4* src = skel->player.getBoneSkinMatrices();
            if (src != nullptr) {
                std::memcpy(skel->skinMatrices, src,
                            skel->jointCount * sizeof(ayt::math::Float4x4));
            }

            // Phase 1.5 (2026-07-26) — AnimNotify EventBus bridge.
            //
            // The player recorded any marker crossings during the tick
            // (above). Drain the queue now (after memcpy so subscribers
            // can safely read the up-to-date skin matrices) and emit
            // each record as an AnimNotifyEvent on the engine bus. The
            // emit is synchronous + main-thread, so subscribers receive
            // their events before this system returns. The optional
            // host sink path is independent — that one fired inside
            // tick() already.
            //
            // We capture the shared_ptr lookup once; the loop body uses
            // a stable pointer to the IAnimation's name.
            const std::string& clipPathKey = _clipCache.contains(anim->clipPath)
                ? anim->clipPath
                : std::string();
            const ayt::resource::IAnimation* clipRes = clipPathKey.empty()
                ? nullptr
                : _clipCache[clipPathKey].get();
            const char* clipNameStable = clipRes ? clipRes->getName() : "unknown";
            const std::uint32_t entityId = e->getId();

            const auto& records = skel->player.consumePendingNotifies();
            if (!records.empty()) {
                ayt::event::EventBus& bus = ayt::event::EventBus::instance();
                for (const auto& rec : records) {
                    bus.emit<ayt::anim::AnimNotifyEvent>(
                        ayt::anim::AnimNotifyEvent{
                            entityId,
                            clipNameStable,
                            rec.name,
                            rec.time,
                            rec.payload,
                        });
                }
            }

            static uint32_t s_poseLog = 0;
            if (s_poseLog < 8) {
                const int spineIdx = skel->skeleton.findBone("spine");
                const int rootIdx  = skel->skeleton.findBone("root");
                std::fprintf(stderr,
                             "[AnimationSystem] pose log=%u t=%.3f dt=%.4f "
                             "spineIdx=%d rootIdx=%d",
                             s_poseLog,
                             skel->player.getTime(),
                             dt,
                             spineIdx,
                             rootIdx);
                if (spineIdx >= 0
                    && static_cast<uint32_t>(spineIdx) < skel->jointCount) {
                    std::fprintf(stderr,
                                 " skinSpineTy=%.4f",
                                 skinMatrixTranslationY(skel->skinMatrices[spineIdx]));
                }
                if (rootIdx >= 0
                    && static_cast<uint32_t>(rootIdx) < skel->jointCount) {
                    std::fprintf(stderr,
                                 " skinRootTy=%.4f",
                                 skinMatrixTranslationY(skel->skinMatrices[rootIdx]));
                }
                std::fprintf(stderr, "\n");
                std::fflush(stderr);
                ++s_poseLog;
            }
        }
    }
}

void registerAnimationSystem()
{
    // GL-01: idempotent across World::shutdown. The static-guard pattern
    // here used to skip re-registration on repeat calls, but that made
    // the system vanish after the first World::shutdown()/initialize()
    // cycle (the guard stayed set, _systems was cleared). The guard in
    // bootstrapModule() is the only one we need — the unit test
    // animation_system_priority_before_render_systems relies on this
    // function being safe to call after a World reset.
    World::instance().registerSystem<AnimationSystem>(AnimationSystem::kPriority);
}

} // namespace ayt::entity