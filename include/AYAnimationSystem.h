#pragma once
// AYAnimationSystem.h — Phase 1 AN-03: per-frame tick + skeleton pose
// evaluation. Runs BEFORE the render systems (priority 450 < 500) so
// the per-bone skin matrices are fresh when the renderer reads them.

#include <IAYEntity.h>

#include <ayanimation/Animation.h>

#include <string>
#include <unordered_map>

namespace ayt::entity
{

class AnimationSystem : public ISystem {
public:
    const char* getName() const override { return "AnimationSystem"; }
    void onUpdate(float dt) override;

    // Exposed for tests / debug only. Not part of the ISystem contract.
    static constexpr int kPriority = 450;

private:
    // Phase 1: cache loaded clips by path so multiple entities sharing
    // a clip don't re-parse. Animations are copyable (vector-of-tracks
    // is copyable); this map owns one copy per unique path.
    std::unordered_map<std::string, ayt::anim::Animation> _clipCache;
};

void registerAnimationSystem();

} // namespace ayt::entity