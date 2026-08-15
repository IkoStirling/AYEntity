#pragma once
// AYEntity/AnimationSystem.h — Phase 1 AN-03: per-frame tick + skeleton pose
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
//
// Phase 1.5 Multi-Source Stack (2026-07-27): AnimationComponent gains
// additiveLayers[] — each entry binds to one of AnimationPlayer's 8
// AdditiveSlot instances. The bridge pushes per-slot state via the new
// per-slot setters; rebind-detection maps upgrade to nested
// unordered_map<const Entity*, unordered_map<uint32_t, T>> so each
// slot gets its own change-detection cache. The legacy single-slot
// scalar fields (additiveClipPath / blendWeight / syncToBase / ...) on
// AnimationComponent are STILL honored as a fallback when
// additiveLayers is empty — so P1.3/P1.4 authored scenes keep working.

#include <AYEntity/IEntity.h>

#include <AYResource/assetsDefs/IAnimation.h>

#include <cstdint>
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

    // Phase 1.3 / P1.5 (2026-07-27) — Additive clip cache. Shared across
    // ALL additive slots (base + every layer). Path-keyed so multiple
    // entities / slots referencing the same .ayanm share the parsed
    // tracks. ResourceManager keeps the resource alive as long as a
    // shared_ptr exists.
    //
    // UPGRADE-HOOK(P1.5 → resolved): the single P1.3 _additiveClipCache
    // is unchanged at the storage level (path-keyed dedup is the same
    // problem for any number of slots). Per-slot state lives in the
    // nested rebind-detection maps below.
    std::unordered_map<std::string,
                       std::shared_ptr<ayt::resource::IAnimation>> _additiveClipCache;

    // P1.5 — Nested rebind-detection maps. Each rebind map is keyed by
    // (entity → slot index → last-pushed value). A setter is invoked
    // only when the slot's component field actually changes since the
    // last frame (mirroring the P1.3 _lastAppliedAdditivePath pattern).
    //
    // The single-slot P1.3 _lastAppliedAdditivePath is replaced by a
    // nested map keyed by slot index; the P1.4 rebind maps likewise.
    // The bridge uses these to skip per-frame setter calls when a
    // component field is unchanged.
    //
    // Inner-map type alias keeps the four nested map declarations
    // readable (one per P1.4 flag pair + one for the path itself).
    template <typename T>
    using PerSlotMap = std::unordered_map<uint32_t, T>;

    // Last additive clip path pushed per (entity, slot).
    std::unordered_map<const Entity*, PerSlotMap<std::string>>
        _lastAppliedAdditivePaths;

    // P1.4 cross-fade rebind maps (per-slot generalisation). Each
    // (entity, slot) pair carries its own last-applied value so two
    // slots on the same entity can have different syncToBase flags.
    std::unordered_map<const Entity*, PerSlotMap<bool>>    _lastAppliedSyncToBase;
    std::unordered_map<const Entity*, PerSlotMap<bool>>    _lastAppliedRefPoseCapture;
    std::unordered_map<const Entity*, PerSlotMap<float>>   _lastAppliedBlendCurveDuration;
    std::unordered_map<const Entity*, PerSlotMap<uint8_t>> _lastAppliedBlendCurveEasing;

    // P2.2 (2026-08-03) — Skeleton Mask rebind-detection map. Keyed by
    // entity only (mask is a single resource per entity, not per-slot).
    // The bridge rebinds the player's mask when this value differs from
    // AnimationComponent::maskPath. On load failure the failed path is
    // latched here so subsequent ticks are no-ops (no per-frame
    // ResourceManager::load retries, no log spam).
    std::unordered_map<const Entity*, std::string> _lastAppliedMaskPath;
};

void registerAnimationSystem();

} // namespace ayt::entity