#pragma once
// AYAnimationComponent.h — Phase 1 E-02: ECS handle for the .ayanm
// clip an entity should play. Drives AnimationPlayer state on the
// paired SkeletonComponent.

#include <IAYEntity.h>

#include <string>

namespace ayt::entity
{

#define AY_CURRENT_CLASS AnimationComponent
struct AnimationComponent : public IComponent {
    const char* getName() const override { return "AnimationComponent"; }

    // Playback flags. Defaults match the demo's expectations
    // (auto-play + loop a short clip until the user pauses).
    // The fields are declared via AY_PROPERTY (which expands to
    // `Type name;` plus a serializer registrar). Default values
    // are set in the constructor because the macro form does not
    // support `= init`.
    AY_PROPERTY(std::string, clipPath, kAttrSerialize)
    AY_PROPERTY(bool,        autoplay, kAttrSerialize)
    AY_PROPERTY(bool,        looping,  kAttrSerialize)
    AY_PROPERTY(float,       playRate, kAttrSerialize)
    // Phase 1.2 (P1.2): Additive Layer 1 weight. Forwarded every frame to
    // AnimationPlayer::setAdditiveWeight by AnimationSystem::onUpdate. The
    // setter saturates to [0, 1]; default 1.0f means "additive tracks (if
    // any) are fully on". Pre-P1.2 builds leave the field at the ctor
    // default (1.0f) — bit-identical behavior for clips with no Additive
    // tracks.
    AY_PROPERTY(float,       additiveWeight, kAttrSerialize)

    AnimationComponent() {
        autoplay = true;
        looping  = true;
        playRate = 1.0f;
        additiveWeight = 1.0f;
    }

    bool isValid() const { return !clipPath.empty(); }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity