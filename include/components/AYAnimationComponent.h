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
    // Phase 1.3 (P1.3) — Additive Layer 2 (Cross-Fade) bridge fields.
    // additiveClipPath empty (default) ⇒ no additive layer is bound; the
    // per-frame push degrades to setAdditiveSource(nullptr). P1.2's
    // additiveWeight becomes blendWeight in the canonical P1.3 API
    // (see AnimationPlayer::setBlendWeight), but we keep the field name
    // for serializer round-trip compat — AnimationSystem forwards it
    // through the deprecated setAdditiveWeight inline-forward wrapper
    // OR through the new setBlendWeight setter directly. We use the new
    // canonical name setBlendWeight here so the per-frame path is
    // unambiguous about which contract it satisfies.
    //
    // UPGRADE-HOOK(P1.4): additivePlayRate + additiveLooping →
    //   syncToBase option (single bool on the player).
    // UPGRADE-HOOK(P1.5): blendWeight + additiveWeight merge into one
    //   per-source weight map.
    AY_PROPERTY(std::string, additiveClipPath, kAttrSerialize)
    AY_PROPERTY(float,       additivePlayRate, kAttrSerialize)
    AY_PROPERTY(float,       blendWeight,      kAttrSerialize)
    // P1.4 — Cross-Fade full ship. Six new fields layered on top of the
    // P1.3 dual-source state machine. Each has its own bridge entry point
    // on AnimationPlayer (see AYAnimationSystem.cpp for the per-frame push).
    //
    //   syncToBase (default false): additive axis lock-step to _time
    //     (UE UAnimMontage::bForceRootLock style; replaces the P1.3
    //     "independent axis" default).
    //   refPoseCapture (default false): additive base = post-Phase-1a
    //     captured pose rather than the skeleton's bind pose
    //     (replaces the P1.2 "ref-pose-at-frame-0" authoring assumption
    //     with a runtime capture).
    //   blendCurveFrom / blendCurveTo / blendCurveDuration /
    //     blendCurveEasing: the four knobs of blendWeightOverTime().
    //     blendCurveDuration = 0 (default) ⇒ the curve path is OFF
    //     and the static blendWeight above wins. blendCurveEasing is
    //     a uint8_t rather than a typed enum to keep the IComponent
    //     surface POCO (the runtime cast to ayt::anim::BlendEasing
    //     happens at the bridge boundary inside AYAnimationSystem).
    //
    // All six are kAttrSerialize so they round-trip through the editor
    // editor scene format introduced alongside Phase 1.5.
    AY_PROPERTY(bool,        syncToBase,        kAttrSerialize)
    AY_PROPERTY(bool,        refPoseCapture,    kAttrSerialize)
    AY_PROPERTY(float,       blendCurveFrom,    kAttrSerialize)
    AY_PROPERTY(float,       blendCurveTo,      kAttrSerialize)
    AY_PROPERTY(float,       blendCurveDuration,kAttrSerialize)
    AY_PROPERTY(uint8_t,     blendCurveEasing,  kAttrSerialize)

    AnimationComponent() {
        autoplay = true;
        looping  = true;
        playRate = 1.0f;
        additiveWeight = 1.0f;     // P1.2 — kept for serializer compat
        additiveClipPath = "";     // P1.3 — empty = no additive layer
        additivePlayRate = 1.0f;
        blendWeight = 1.0f;        // P1.3 — canonical new name
        syncToBase = false;        // P1.4 — additive independent axis (P1.3 default)
        refPoseCapture = false;    // P1.4 — additive base = rest pose (P1.3 default)
        blendCurveFrom = 0.0f;     // P1.4 — curve start weight (default OFF because
        blendCurveTo = 1.0f;       //           blendCurveDuration = 0 ⇒ static fallback)
        blendCurveDuration = 0.0f; // P1.4 — 0 = curve OFF (mirrors player default)
        blendCurveEasing = 0;      // P1.4 — 0 = ayt::anim::BlendEasing::Linear
    }

    bool isValid() const { return !clipPath.empty(); }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity