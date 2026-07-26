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

        // Phase 1.3 (P1.3) — Additive Layer 2 (Cross-Fade) bridge.
        // The additive source is lazily loaded into _additiveClipCache
        // (mirror of _clipCache). When additiveClipPath is empty we
        // explicitly unbind via setAdditiveSource(nullptr) so the layer
        // is OFF — this drives Phase 1b to a no-op per INV-1. When
        // non-empty, we check rebind detection on the last applied path
        // and call setAdditiveSource only when it changes (mirrors the
        // _entityBoundClip pattern above).
        //
        // blendWeight (P1.3 canonical name) is forwarded to the player
        // via setBlendWeight — the new setter replaces setAdditiveWeight
        // for layer-level mixing. We still call setAdditiveWeight above
        // because that field is the P1.2 per-track additive weight
        // preserved for serializer compat — P1.2 ships both Layer 1
        // (per-track) and the new Layer 2 (cross-clip) blend semantics
        // through the same scalar (named _blendWeight in the player).
        //
        // UPGRADE-HOOK(P1.4): syncToBase option (a single bool on the
        //   player) would gate the additive axis advance in tick().
        // UPGRADE-HOOK(P1.5): per-source weight map replaces the single
        //   blendWeight with vector<float> indexed by layer slot.
        if (!anim->additiveClipPath.empty()) {
            // Lazy-load the additive clip.
            if (_additiveClipCache.find(anim->additiveClipPath)
                == _additiveClipCache.end()) {
                auto addRes = ayt::resource::ResourceManager::instance()
                                  .load<ayt::resource::IAnimation>(
                                      anim->additiveClipPath);
                if (!addRes) {
                    std::fprintf(stderr,
                                 "[AnimationSystem] loadAdditiveAnimation('%s') failed\n",
                                 anim->additiveClipPath.c_str());
                    // Continue without binding the additive layer so the
                    // base animation still plays. INV-4 degenerate state
                    // (additive-clip set but base==null) is impossible
                    // here because we've already checked skel->loaded.
                    continue;
                }
                _additiveClipCache.emplace(anim->additiveClipPath, addRes);
            }

            // Rebind detection: only call setAdditiveSource when the
            // path actually changes (or is first-time bound). Mirrors
            // the base rebind pattern above.
            const std::string& lastAdd = _lastAppliedAdditivePath[e];
            if (lastAdd != anim->additiveClipPath) {
                _lastAppliedAdditivePath[e] = anim->additiveClipPath;
                skel->player.setAdditiveSource(
                    _additiveClipCache[anim->additiveClipPath].get(),
                    anim->additivePlayRate,
                    /*loop=*/true);
            }
            // Always forward the layer weight — host may have updated
            // blendWeight on the component since last frame.
            skel->player.setBlendWeight(anim->blendWeight);
        } else {
            // additiveClipPath empty → layer OFF. Unbind if previously
            // bound (cheap path: setAdditiveSource(nullptr) on an
            // already-null source is a no-op).
            const std::string& lastAdd = _lastAppliedAdditivePath[e];
            if (!lastAdd.empty()) {
                _lastAppliedAdditivePath[e] = std::string();
                skel->player.setAdditiveSource(nullptr);
            }
        }

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

            // Phase 1.3 (P1.3) — Additive Layer 2 (Cross-Fade) EventBus
            // bridge. Drain the additive source's notify queue and emit
            // each record on the same AnimNotifyEvent channel. The MVP
            // does NOT tag records as base vs additive — subscribers that
            // care which source fired a given marker must use context
            // (e.g. a thread-local flag the host sets before each tick,
            // or filter by clipName when host-authored clips disambiguate).
            //
            // UPGRADE-HOOK(P1.5): add a `source` enum field to
            //   AnimNotifyEvent (Base | Additive) and an optional
            //   dedup-by-(time, name) merge layer so host code gets a
            //   single sorted stream with explicit source attribution.
            //
            // The additive source's clipName comes from
            // _additiveClipCache — same stable-pointer lifetime contract
            // as base (IAnimation-owned). When the additive source is
            // not bound, this drain is empty (the player never enqueued).
            if (skel->player.getPendingNotifyCountAdditive() > 0) {
                // Look up the additive clip's stable name pointer. We
                // mirror the base clipNameStable resolution path.
                const std::string& addClipKey =
                    _lastAppliedAdditivePath.count(e)
                        ? _lastAppliedAdditivePath[e]
                        : std::string();
                const ayt::resource::IAnimation* addClipRes =
                    !addClipKey.empty() && _additiveClipCache.count(addClipKey)
                        ? _additiveClipCache[addClipKey].get()
                        : nullptr;
                const char* addClipNameStable =
                    addClipRes ? addClipRes->getName() : "additive-unknown";

                const auto& addRecords =
                    skel->player.consumePendingNotifiesAdditive();
                if (!addRecords.empty()) {
                    ayt::event::EventBus& bus = ayt::event::EventBus::instance();
                    for (const auto& rec : addRecords) {
                        bus.emit<ayt::anim::AnimNotifyEvent>(
                            ayt::anim::AnimNotifyEvent{
                                entityId,
                                addClipNameStable,
                                rec.name,
                                rec.time,
                                rec.payload,
                            });
                    }
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