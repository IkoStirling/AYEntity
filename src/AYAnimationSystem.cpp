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
//
// P1.5 (2026-07-27) — Multi-Source Stack bridge. AnimationComponent
// gains additiveLayers[]. When non-empty, the bridge iterates per slot
// and pushes via the new per-slot setters; rebind-detection maps upgrade
// to nested (entity → slot index) keys. When empty, the legacy
// single-slot scalar fields (additiveClipPath / blendWeight / ...) keep
// working — P1.3/P1.4 authored scenes don't break. Merged notify dispatch
// path; AnimNotifyEvent.sourceTag carries the per-record source attribution.
//
// P1.7 (2026-07-27) — Shared Skeleton Tick Cache. SkeletonComponent
// now holds std::shared_ptr<const ISkeleton> instead of by-value
// Skeleton. The lazy-load block keeps a strong reference into
// ResourceManager and binds the player with the same shared_ptr.
// N entities with the same skeletonPath share one ISkeleton asset —
// the by-value Skeleton copy loop (setBoneCount + setBone × N bones)
// is gone. AssetBoneCache (see AYAnimation/AssetBoneCache.h) gives
// every AnimationPlayer that binds the same ISkeleton* shared
// bone-name → bone-index resolution.

#include "AYAnimationSystem.h"

#include <components/AYAnimationComponent.h>
#include <components/AYSkeletonComponent.h>

#include <AYEntity.h>
#include <AYWorld.h>
#include <IAYSkeleton.h>
#include <assetsImpl/AYSkeleton.h>   // P1.7 — for static_pointer_cast<Skeleton>
#include <AYResourceManager.h>

// Phase 1.5 — AnimNotify bridge: drain the player's per-frame fired
// markers and emit each as AnimNotifyEvent on the engine EventBus.
#include <ayanimation/AnimNotifyEvent.h>
#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/ISkeletonMask.h>    // P2.2 — typed mask load
#include <ayevent/EventBus.h>

#include <algorithm>
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

// P1.5 — pull the additive clip from the cache (lazy-load if absent) and
// return a stable pointer to the IAnimation (or nullptr on failure). The
// cache is path-keyed so multiple slots referencing the same .ayanm share
// the parsed track data. Caller must check the return for nullptr before
// dereferencing.
const ayt::resource::IAnimation* loadAdditiveClip(
    std::unordered_map<std::string,
                       std::shared_ptr<ayt::resource::IAnimation>>& cache,
    const std::string& path,
    const char* debugTag)
{
    auto it = cache.find(path);
    if (it == cache.end()) {
        auto res = ayt::resource::ResourceManager::instance()
                       .load<ayt::resource::IAnimation>(path);
        if (!res) {
            std::fprintf(stderr,
                         "[AnimationSystem] loadAdditiveAnimation('%s') "
                         "%s failed\n",
                         path.c_str(), debugTag);
            return nullptr;
        }
        cache.emplace(path, res);
        it = cache.find(path);
    }
    return it->second.get();
}

// P1.5 — read the last-pushed value for (entity, slot) from a nested
// rebind map; returns false if no prior push has happened for this slot
// (first-time bind — treated as "last value differs from current").
template <typename T>
bool readLastApplied(
    const std::unordered_map<const Entity*,
                             std::unordered_map<uint32_t, T>>& map,
    const Entity* e, uint32_t slot, const T*& out)
{
    auto eIt = map.find(e);
    if (eIt == map.end()) return false;
    auto sIt = eIt->second.find(slot);
    if (sIt == eIt->second.end()) return false;
    out = &sIt->second;
    return true;
}

template <typename T>
void writeLastApplied(
    std::unordered_map<const Entity*,
                       std::unordered_map<uint32_t, T>>& map,
    const Entity* e, uint32_t slot, const T& value)
{
    map[e][slot] = value;
}

template <typename T>
void eraseLastApplied(
    std::unordered_map<const Entity*,
                       std::unordered_map<uint32_t, T>>& map,
    const Entity* e, uint32_t slot)
{
    auto eIt = map.find(e);
    if (eIt == map.end()) return;
    eIt->second.erase(slot);
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

        // First-time lazy-load. Skeletons are loaded ONCE per
        // unique skeletonPath (ResourceManager caches by path); the
        // shared_ptr retained in SkeletonComponent pins the asset so
        // LRU trim cannot evict it while the entity is alive. N
        // entities sharing a skeletonPath all hold shared_ptrs to
        // the SAME ISkeleton instance — zero per-entity duplication.
        if (!skel->loaded) {
            if (skel->layoutMagic != SkeletonComponent::kLayoutMagic) {
                std::fprintf(stderr,
                             "[AnimationSystem] SkeletonComponent layout mismatch "
                             "(magic=0x%08X expected=0x%08X). Full rebuild AYEntity "
                             "+ demo — stale .obj still has embedded AnimationPlayer.\n",
                             skel->layoutMagic,
                             SkeletonComponent::kLayoutMagic);
                std::fflush(stderr);
                continue;
            }
            auto skelRes = ayt::resource::ResourceManager::instance()
                              .load<ayt::resource::ISkeleton>(skel->skeletonPath);
            if (!skelRes || skelRes->getBoneCount() == 0) {
                std::fprintf(stderr,
                             "[AnimationSystem] loadSkeleton('%s') failed\n",
                             skel->skeletonPath.c_str());
                continue;
            }
            // P1.7 — pin the asset via shared_ptr<Skeleton>. The
            // ResourceManager::load<ISkeleton> returns
            // shared_ptr<ISkeleton>; static_pointer_cast down to
            // shared_ptr<Skeleton> (Skeleton derives from ISkeleton
            // publicly so the cast is well-defined) so the component
            // can still expose the concrete type for tests that build
            // programmatically. AnimationPlayer::setSkeleton accepts
            // shared_ptr<const ISkeleton> — shared_ptr<Skeleton>
            // implicitly converts.
            skel->skeleton = std::static_pointer_cast<ayt::resource::Skeleton>(
                skelRes);
            skel->jointCount = static_cast<uint32_t>(
                skel->skeleton->getBoneCount());
            if (skel->jointCount == 0) {
                skel->loaded = true;  // mark loaded but unusable
                continue;
            }

            delete[] skel->skinMatrices;
            skel->skinMatrices = new ayt::math::Float4x4[skel->jointCount];
            {
                const ayt::math::Float4x4 id = ayt::math::Float4x4::identity();
                for (uint32_t i = 0; i < skel->jointCount; ++i) {
                    std::memcpy(&skel->skinMatrices[i], &id, sizeof(id));
                }
            }

            // Bind-pose / mesh-only (empty clipPath): identity skin
            // matrices are enough — do NOT call setSkeleton (avoids
            // allocating 543-bone TRS buffers when no clip will play).
            // AnimationPlayer is bound lazily when a clipPath appears.
            if (!anim->clipPath.empty()) {
                if (!skel->player) {
                    skel->player = ayt::anim::AnimationPlayer::create();
                }
                if (!skel->player) {
                    std::fprintf(stderr,
                                 "[AnimationSystem] AnimationPlayer::create failed\n");
                    continue;
                }
                std::fprintf(stderr,
                             "[AnimationSystem] setSkeleton this=%p player=%p bones=%u\n",
                             (void*)skel, (void*)skel->player.get(), skel->jointCount);
                std::fflush(stderr);
                skel->player->setSkeleton(skel->skeleton);
            } else {
                std::fprintf(stderr,
                             "[AnimationSystem] bind-pose skip setSkeleton joints=%u\n",
                             skel->jointCount);
                std::fflush(stderr);
            }

            skel->loaded = true;
        }

        if (!skel->loaded || skel->jointCount == 0) continue;

        // P2.2 (2026-08-03) — Skeleton Mask rebind. Runs BEFORE the
        // clipPath-empty early-return below so a bind-pose entity
        // (clipPath="") can still have its mask cleared / latched.
        // When the player is null (no clipPath yet, no direct API
        // bind), the rebind only updates _lastAppliedMaskPath so
        // subsequent ticks don't re-attempt the load.
        const std::string& lastMask = _lastAppliedMaskPath.count(e)
            ? _lastAppliedMaskPath[e] : std::string();
        if (lastMask != anim->maskPath) {
            if (anim->maskPath.empty()) {
                // User cleared the mask → drop the player's reference.
                if (skel->player) {
                    skel->player->clearSkeletonMask();
                }
                _lastAppliedMaskPath[e] = "";
            } else {
                auto maskRes = ayt::resource::ResourceManager::instance()
                                  .load<ayt::anim::ISkeletonMask>(
                                      anim->maskPath);
                if (!maskRes) {
                    // Fail-soft. Latch the failed path so we do not
                    // retry ResourceManager::load every frame.
                    _lastAppliedMaskPath[e] = anim->maskPath;
                    std::fprintf(stderr,
                                 "[AnimationSystem] loadSkeletonMask('%s') "
                                 "failed (.aymask loader deferred per §4.2.1)\n",
                                 anim->maskPath.c_str());
                    std::fflush(stderr);
                } else if (skel->player) {
                    // shared_ptr copies into the player; the
                    // ResourceManager-side cache holds the strong
                    // reference until LRU trim.
                    skel->player->setSkeletonMask(maskRes);
                    _lastAppliedMaskPath[e] = anim->maskPath;
                }
                // else: mask loaded but no player exists yet (rare
                // ordering — user set maskPath before clipPath). We
                // drop the loaded resource; the next lazy-load of
                // the player will pick up maskPath again on the
                // tick the clipPath becomes non-empty.
            }
        }

        // Bind-pose / mesh-only preview: empty clipPath keeps identity
        // skin matrices. Skip the per-frame lazy-load block (do NOT
        // call ResourceManager::load("") every frame). The P2.2 mask
        // rebind happened ABOVE this check; see
        // SkeletonMaskBridgeTests #6 for the bind-pose-clear path.
        if (anim->clipPath.empty()) {
            continue;
        }

        // Clip path was empty at load but set later (inspector) — bind now.
        if (!skel->player) {
            skel->player = ayt::anim::AnimationPlayer::create();
            if (!skel->player) continue;
            skel->player->setSkeleton(skel->skeleton);
        }

        // Lazy-load the base clip if not cached.
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

        skel->player->setPlayRate(anim->playRate);
        skel->player->setLoop(anim->looping);
        // Phase 1.2 (P1.2) — Additive Layer 1 weight passthrough. The
        // AnimationPlayer's setter saturates to [0, 1] so a typo on the
        // engine side cannot NaN a quaternion. Default 1.0f on both sides
        // keeps pre-P1.2 clips (no Additive tracks) bit-identical.
        // P1.5: this still drives the per-track Additive semantics for
        // tracks flagged AnimBlendMode::Additive inside the BASE clip;
        // it does NOT touch per-slot additive weights (those go through
        // setAdditiveLayerWeight below).
        // P1.6 cleanup: AnimationComponent::additiveWeight field name is
        // preserved for serializer round-trip compat with P1.2/P1.3/P1.4
        // authored scenes; the per-frame push now goes through the
        // canonical P1.3 setBlendWeight setter (the inline-forward
        // setAdditiveWeight wrapper was removed in P1.6).
        skel->player->setBlendWeight(anim->additiveWeight);

        // Phase 1.5 (P1.5) — Multi-Source Stack bridge. Two paths:
        //
        //   (a) anim->additiveLayers.size() > 0 → per-slot push loop
        //       over each AdditiveLayerSpec; the per-slot setters on
        //       AnimationPlayer are the canonical entry points.
        //
        //   (b) anim->additiveLayers.size() == 0 → legacy P1.3/P1.4
        //       single-slot path using the scalar fields
        //       (additiveClipPath / blendWeight / syncToBase / ...).
        //       Preserves byte-for-byte compat with P1.3/P1.4 authored
        //       scenes.
        //
        // Rebind detection in both paths uses the nested
        // _lastApplied* maps keyed by (entity → slot index). Slot 0
        // is the legacy fallback path's target; the multi-slot path
        // uses the slot index from each AdditiveLayerSpec (or its
        // position in the array if slotIndex == UINT32_MAX sentinel).
        if (!anim->additiveLayers.empty()) {
            // (a) Per-slot push loop. We iterate over the host's
            // additiveLayers[]; each entry binds to slot index
            // (slotIndex if explicitly set, else position in the
            // vector). Slots beyond kMaxAdditiveSlots=8 are silently
            // skipped (defensive).
            constexpr uint32_t kMaxSlots = 8;   // = kMaxAdditiveSlots
            const size_t n = std::min(anim->additiveLayers.size(),
                                      static_cast<size_t>(kMaxSlots));
            for (size_t i = 0; i < n; ++i) {
                const AdditiveLayerSpec& spec = anim->additiveLayers[i];
                const uint32_t slotIdx =
                    (spec.slotIndex == UINT32_MAX)
                        ? static_cast<uint32_t>(i)
                        : spec.slotIndex;
                // Bound check again per-slot — slotIndex might be
                // out of range even when the array size is fine.
                if (slotIdx >= kMaxSlots) continue;

                // 1. Source bind (rebind detection on path).
                if (!spec.additiveClipPath.empty()) {
                    const std::string& lastPath =
                        _lastAppliedAdditivePaths.count(e)
                            ? _lastAppliedAdditivePaths[e][slotIdx]
                            : std::string();
                    if (lastPath != spec.additiveClipPath) {
                        const ayt::resource::IAnimation* clip =
                            loadAdditiveClip(_additiveClipCache,
                                             spec.additiveClipPath,
                                             "slot");
                        if (clip != nullptr) {
                            _lastAppliedAdditivePaths[e][slotIdx] =
                                spec.additiveClipPath;
                            skel->player->setAdditiveLayerSource(
                                slotIdx, clip,
                                spec.additivePlayRate,
                                spec.looping);
                        }
                    }
                } else {
                    // Empty path → slot OFF (rebind-detection on
                    // path; only call clear when we previously bound).
                    const std::string* lastPath = nullptr;
                    readLastApplied(_lastAppliedAdditivePaths, e, slotIdx,
                                    lastPath);
                    if (lastPath != nullptr && !lastPath->empty()) {
                        eraseLastApplied(_lastAppliedAdditivePaths, e, slotIdx);
                        skel->player->clearAdditiveLayerSource(slotIdx);
                    }
                }

                // 2. Per-slot weight (always forwarded; saturating
                // setter on the player side).
                skel->player->setAdditiveLayerWeight(slotIdx,
                                                    spec.blendWeight);

                // 3. syncToBase rebind detection.
                const bool* lastSync = nullptr;
                const bool hasLastSync = readLastApplied(
                    _lastAppliedSyncToBase, e, slotIdx, lastSync);
                const bool wantSync = spec.syncToBase;
                if (!hasLastSync || *lastSync != wantSync) {
                    writeLastApplied(_lastAppliedSyncToBase, e, slotIdx,
                                     wantSync);
                    skel->player->setAdditiveLayerSyncToBase(slotIdx,
                                                             wantSync);
                }

                // 4. refPoseCapture rebind detection.
                const bool* lastRef = nullptr;
                const bool hasLastRef = readLastApplied(
                    _lastAppliedRefPoseCapture, e, slotIdx, lastRef);
                const bool wantRef = spec.refPoseCapture;
                if (!hasLastRef || *lastRef != wantRef) {
                    writeLastApplied(_lastAppliedRefPoseCapture, e, slotIdx,
                                     wantRef);
                    skel->player->setAdditiveLayerRefPoseCapture(slotIdx,
                                                                wantRef);
                }

                // 5. blendWeightOverTime rebind detection. Only call
                // when (duration, easing) actually changes; from/to
                // shifts land on the next curve anchor naturally.
                const float wantDur = spec.blendCurveDuration;
                const uint8_t wantEase = spec.blendCurveEasing;
                const float* lastDurPtr = nullptr;
                const uint8_t* lastEasePtr = nullptr;
                const bool hasLastDur = readLastApplied(
                    _lastAppliedBlendCurveDuration, e, slotIdx, lastDurPtr);
                const bool hasLastEase = readLastApplied(
                    _lastAppliedBlendCurveEasing, e, slotIdx, lastEasePtr);
                const float lastDur = hasLastDur ? *lastDurPtr : -1.0f;
                const uint8_t lastEase = hasLastEase ? *lastEasePtr : 0xFF;
                if (wantDur > 0.0f
                    && (!hasLastDur || !hasLastEase
                        || lastDur != wantDur || lastEase != wantEase)) {
                    writeLastApplied(_lastAppliedBlendCurveDuration, e,
                                     slotIdx, wantDur);
                    writeLastApplied(_lastAppliedBlendCurveEasing, e,
                                     slotIdx, wantEase);
                    const ayt::anim::BlendEasing easingEnum =
                        (wantEase < 5)
                            ? static_cast<ayt::anim::BlendEasing>(wantEase)
                            : ayt::anim::BlendEasing::Linear;
                    skel->player->blendLayerWeightOverTime(
                        slotIdx,
                        spec.blendCurveFrom,
                        spec.blendCurveTo,
                        wantDur,
                        easingEnum);
                } else if (wantDur == 0.0f
                           && hasLastDur && lastDur != 0.0f) {
                    writeLastApplied(_lastAppliedBlendCurveDuration, e,
                                     slotIdx, 0.0f);
                    skel->player->cancelLayerBlendCurve(slotIdx);
                }
            }
        } else if (!anim->additiveClipPath.empty()) {
            // (b) Legacy P1.3/P1.4 single-slot path — additiveClipPath
            // populates slot 0. Mirror the per-slot loop body above
            // using the scalar fields on AnimationComponent.
            //
            // Lazy-load the additive clip.
            const ayt::resource::IAnimation* clip = loadAdditiveClip(
                _additiveClipCache, anim->additiveClipPath, "legacy");
            if (clip != nullptr) {
                const std::string& lastAdd = _lastAppliedAdditivePaths
                    .count(e) ? _lastAppliedAdditivePaths[e][0u]
                              : std::string();
                if (lastAdd != anim->additiveClipPath) {
                    _lastAppliedAdditivePaths[e][0u] = anim->additiveClipPath;
                    skel->player->setAdditiveSource(
                        clip, anim->additivePlayRate, /*loop=*/true);
                }
                // Always forward the layer weight.
                skel->player->setBlendWeight(anim->blendWeight);

                // P1.4 syncToBase / refPoseCapture / curve knobs.
                const bool wantSync = anim->syncToBase;
                const bool* lastSyncPtr = nullptr;
                const bool hasLastSync = readLastApplied(
                    _lastAppliedSyncToBase, e, 0u, lastSyncPtr);
                if (!hasLastSync || *lastSyncPtr != wantSync) {
                    writeLastApplied(_lastAppliedSyncToBase, e, 0u,
                                     wantSync);
                    skel->player->setAdditiveSyncToBase(wantSync);
                }

                const bool wantRef = anim->refPoseCapture;
                const bool* lastRefPtr = nullptr;
                const bool hasLastRef = readLastApplied(
                    _lastAppliedRefPoseCapture, e, 0u, lastRefPtr);
                if (!hasLastRef || *lastRefPtr != wantRef) {
                    writeLastApplied(_lastAppliedRefPoseCapture, e, 0u,
                                     wantRef);
                    skel->player->setAdditiveRefPoseCapture(wantRef);
                }

                const float wantDur = anim->blendCurveDuration;
                const uint8_t wantEase = anim->blendCurveEasing;
                const float* lastDurPtr = nullptr;
                const uint8_t* lastEasePtr = nullptr;
                const bool hasLastDur = readLastApplied(
                    _lastAppliedBlendCurveDuration, e, 0u, lastDurPtr);
                const bool hasLastEase = readLastApplied(
                    _lastAppliedBlendCurveEasing, e, 0u, lastEasePtr);
                const float lastDur = hasLastDur ? *lastDurPtr : -1.0f;
                const uint8_t lastEase = hasLastEase ? *lastEasePtr : 0xFF;
                if (wantDur > 0.0f
                    && (!hasLastDur || !hasLastEase
                        || lastDur != wantDur || lastEase != wantEase)) {
                    writeLastApplied(_lastAppliedBlendCurveDuration, e,
                                     0u, wantDur);
                    writeLastApplied(_lastAppliedBlendCurveEasing, e,
                                     0u, wantEase);
                    const ayt::anim::BlendEasing easingEnum =
                        (wantEase < 5)
                            ? static_cast<ayt::anim::BlendEasing>(wantEase)
                            : ayt::anim::BlendEasing::Linear;
                    skel->player->blendWeightOverTime(
                        anim->blendCurveFrom,
                        anim->blendCurveTo,
                        wantDur,
                        easingEnum);
                } else if (wantDur == 0.0f
                           && hasLastDur && lastDur != 0.0f) {
                    writeLastApplied(_lastAppliedBlendCurveDuration, e,
                                     0u, 0.0f);
                    skel->player->cancelBlendCurve();
                }
            }
        } else {
            // (c) additiveClipPath empty AND additiveLayers empty →
            // ALL slots OFF. Clear any previously-bound slots and
            // erase the per-slot rebind-detection state for this
            // entity so a future re-bind starts in fresh state.
            //
            // Slot-by-slot clear (sparse — only clear slots that
            // were previously bound). This preserves host intent:
            // a layer that was bound last frame is unbound now.
            auto pathIt = _lastAppliedAdditivePaths.find(e);
            if (pathIt != _lastAppliedAdditivePaths.end()) {
                for (const auto& kv : pathIt->second) {
                    const uint32_t slotIdx = kv.first;
                    if (slotIdx < 8) {
                        skel->player->clearAdditiveLayerSource(slotIdx);
                    }
                }
                _lastAppliedAdditivePaths.erase(pathIt);
            }
            if (_lastAppliedSyncToBase.count(e)) {
                _lastAppliedSyncToBase.erase(e);
            }
            if (_lastAppliedRefPoseCapture.count(e)) {
                _lastAppliedRefPoseCapture.erase(e);
            }
            if (_lastAppliedBlendCurveDuration.count(e)) {
                _lastAppliedBlendCurveDuration.erase(e);
            }
            if (_lastAppliedBlendCurveEasing.count(e)) {
                _lastAppliedBlendCurveEasing.erase(e);
            }
        }

        const std::string& boundClip = _entityBoundClip[e];
        if (boundClip != anim->clipPath) {
            _entityBoundClip[e] = anim->clipPath;
            skel->player->play(_clipCache[anim->clipPath].get());
        }

        if (anim->autoplay) {
            skel->player->tick(dt);
        }

        if (skel->player->isValid()) {
            skel->player->evaluate();
            // P2.1 — pick-non-null base source. The BlendSpaceSystem
            // (priority 430, runs before us) writes
            // `skinMatricesBlendSpace` when the entity carries a
            // BlendSpaceComponent. We treat that as the authoritative
            // base; the additive layers below still layer on top via
            // the unchanged phase 1b path. Entities WITHOUT a
            // BlendSpaceSystem write fall through to the legacy
            // AnimationPlayer::getBoneSkinMatrices() output.
            const ayt::math::Float4x4* srcBS = skel->skinMatricesBlendSpace;
            const ayt::math::Float4x4* srcAP = skel->player->getBoneSkinMatrices();
            const ayt::math::Float4x4* srcUse = (srcBS != nullptr) ? srcBS : srcAP;
            if (srcUse != nullptr) {
                std::memcpy(skel->skinMatrices, srcUse,
                            skel->jointCount * sizeof(ayt::math::Float4x4));
            }

            // Phase 1.5 (2026-07-27) — AnimNotify EventBus bridge via
            // the merged queue. A SINGLE consumePendingNotifiesMerged()
            // call carries AnimNotifySourceTag on each record. The
            // AnimNotifyEvent struct carries sourceTag through to the
            // EventBus so subscribers can route per-source.
            //
            // The merged queue drains BOTH base records AND per-slot
            // records in one call; the previous per-slot notify was
            // emitted with clipName from _lastAppliedAdditivePath[0]
            // only, which silently dropped slot>=1 markers. The merged
            // queue fixes that — every slot's record surfaces with
            // its own clip name pointer.
            //
            // Clip name resolution per record: base records use the
            // entity's bound base clip; additive records use the
            // matching slot's clip. We resolve once per source via
            // the (entity → slot) map.
            const std::uint32_t entityId = e->getId();
            const ayt::event::EventBus& bus = ayt::event::EventBus::instance();
            const std::string& baseClipNameKey = anim->clipPath;
            const ayt::resource::IAnimation* baseClipRes =
                _clipCache.count(baseClipNameKey)
                    ? _clipCache[baseClipNameKey].get()
                    : nullptr;
            const char* baseClipNameStable =
                baseClipRes ? baseClipRes->getName() : "unknown";

            const auto& records = skel->player->consumePendingNotifiesMerged();
            for (const auto& rec : records) {
                const char* clipNameStable = baseClipNameStable;
                if (rec.sourceTag != ayt::anim::AnimNotifySourceTag::Base) {
                    // Additive record — resolve the slot index from the
                    // tag and look up that slot's bound clip.
                    const uint32_t slotIdx = static_cast<uint32_t>(
                        static_cast<uint8_t>(rec.sourceTag)
                        - static_cast<uint8_t>(
                            ayt::anim::AnimNotifySourceTag::Additive_0));
                    const std::string* slotPath = nullptr;
                    if (slotIdx < 8
                        && _lastAppliedAdditivePaths.count(e)
                        && _lastAppliedAdditivePaths[e].count(slotIdx)) {
                        slotPath = &_lastAppliedAdditivePaths[e][slotIdx];
                    }
                    if (slotPath != nullptr && !slotPath->empty()
                        && _additiveClipCache.count(*slotPath)) {
                        clipNameStable =
                            _additiveClipCache[*slotPath]->getName();
                    } else {
                        clipNameStable = "additive-unknown";
                    }
                }
                ayt::event::EventBus& busMut = ayt::event::EventBus::instance();
                busMut.emit<ayt::anim::AnimNotifyEvent>(
                    ayt::anim::AnimNotifyEvent{
                        entityId,
                        clipNameStable,
                        rec.name,
                        rec.time,
                        rec.payload,
                        rec.sourceTag,
                    });
            }

            static uint32_t s_poseLog = 0;
            if (s_poseLog < 8) {
                const int spineIdx = skel->skeleton->findBone("spine");
                const int rootIdx  = skel->skeleton->findBone("root");
                std::fprintf(stderr,
                             "[AnimationSystem] pose log=%u t=%.3f dt=%.4f "
                             "spineIdx=%d rootIdx=%d",
                             s_poseLog,
                             skel->player->getTime(),
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