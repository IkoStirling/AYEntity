#pragma once
// AYAnimationSystem.h — Phase 1 AN-03: per-frame tick + skeleton pose
// evaluation. Runs BEFORE the render systems (priority 450 < 500) so
// the per-bone skin matrices are fresh when the renderer reads them.
//
// P0 (2026-07-26): clip cache now holds shared_ptr<ayt::resource::IAnimation>
// directly. The AnimationPlayer takes the raw interface pointer via
// play(IAnimation*); ResourceManager keeps the resource alive as long as
// any shared_ptr in the cache holds a refcount.
//
// Phase 1.5 (2026-07-26): each entity's AnimationPlayer exposes a per-tick
// queue of crossed AnimNotify markers; we drain that queue with
// consumePendingNotifies() after the skin-matrix memcpy and emit each
// record as an AnimNotifyEvent into the engine EventBus (see AYEventSystem).
// Cross-module game code (audio, VFX, UI, gameplay) subscribes to the
// event type without depending on AYEntity or AYAnimation.

#include <IAYEntity.h>

#include <assetsDefs/IAYAnimation.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace ayt::entity
{

class Entity;

class AnimationSystem : public ISystem {
public:
    const char* getName() const override { return "AnimationSystem"; }
    void onUpdate(float dt) override;

    // Exposed for tests / debug only. Not part of the ISystem contract.
    static constexpr int kPriority = 450;

private:
    // Phase 1: cache loaded clips by path so multiple entities sharing
    // a clip don't re-parse. Each cache slot holds a shared_ptr to the
    // resource::IAnimation; ResourceManager keeps the underlying data
    // alive as long as a shared_ptr exists.
    std::unordered_map<std::string,
                       std::shared_ptr<ayt::resource::IAnimation>> _clipCache;
    // Last clip path bound per entity; play() only runs when this changes.
    std::unordered_map<const Entity*, std::string> _entityBoundClip;

    // Phase 1.3 (P1.3) — Additive Layer 2 (Cross-Fade) bridge:
    //   additive clip cache mirrors _clipCache for the layer's source.
    //   ResourceManager keeps the resource alive as long as a shared_ptr
    //   holds. Independent from _clipCache because the additive source
    //   has its own lazy-load + lifetime.
    //
    //   _lastAppliedAdditivePath mirrors _entityBoundClip for additive
    //   rebind detection. Per-entity, last path that was actually pushed
    //   via setAdditiveSource (could be "" if no layer is active).
    //
    //   UPGRADE-HOOK(P1.5): when a stack of additive layers ships, this
    //   becomes _additiveClipCaches[slotIndex] and _lastAppliedAdditivePaths
    //   [slotIndex].
    std::unordered_map<std::string,
                       std::shared_ptr<ayt::resource::IAnimation>> _additiveClipCache;
    std::unordered_map<const Entity*, std::string> _lastAppliedAdditivePath;

    // P1.4 — Cross-fade full-ship bridge. Four rebind-detection maps
    // mirror the P1.3 _lastAppliedAdditivePath pattern so a setter is
    // only invoked when the host-side component field actually changes,
    // avoiding per-frame churn on the underlying player.
    //
    //   _lastAppliedSyncToBase / _lastAppliedRefPoseCapture are bools:
    //     a flag toggle re-triggers the setter exactly once.
    //   _lastAppliedBlendCurveDuration / _lastAppliedBlendCurveEasing
    //     are the two curve knobs whose change actually requires a
    //     blendWeightOverTime() call. blendCurveFrom / blendCurveTo
    //     shifts do NOT trigger a call (they land on the next curve
    //     anchor naturally; the current in-flight curve is allowed
    //     to finish first).
    std::unordered_map<const Entity*, bool>    _lastAppliedSyncToBase;
    std::unordered_map<const Entity*, bool>    _lastAppliedRefPoseCapture;
    std::unordered_map<const Entity*, float>   _lastAppliedBlendCurveDuration;
    std::unordered_map<const Entity*, uint8_t> _lastAppliedBlendCurveEasing;
};

void registerAnimationSystem();

} // namespace ayt::entity