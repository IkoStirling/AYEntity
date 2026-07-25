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
};

void registerAnimationSystem();

} // namespace ayt::entity