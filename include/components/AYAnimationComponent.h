#pragma once
// AYAnimationComponent.h — Phase 1 E-02: ECS handle for the .ayanm
// clip an entity should play. Drives AnimationPlayer state on the
// paired SkeletonComponent.

#include <IAYEntity.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ayt::entity
{

// P1.5 — per-additive-layer spec. Mirrors one slot in AnimationPlayer's
// vector<AdditiveSlot>. The host populates additiveLayers[0..N-1] to bind
// up to kMaxAdditiveSlots = 8 simultaneous layers (UE LayerObjects[] /
// Unity AnimationLayerMixerPlayable.SetLayerCount cap). Each field is a
// straight copy onto the matching AnimationPlayer setter per frame; see
// AYAnimationSystem.cpp per-slot push loop.
//
// Slot ordering: additiveLayers[i].slotIndex in the IComponent spec
// matches slot[i] in the player (sparse — you can populate slot 4
// without slot 0..3, the bridge pads with empty slots). When the array
// is empty the bridge falls back to the legacy single-slot fields
// (additiveClipPath / blendWeight / etc.) for backward compat with
// P1.3/P1.4 authored scenes.
#define AY_CURRENT_CLASS AdditiveLayerSpec
struct AdditiveLayerSpec {
    // Slot index in AnimationPlayer._additiveSlots. Defaults to its
    // position in additiveLayers[] if left at UINT32_MAX (the bridge
    // auto-fills on first push).
    AY_PROPERTY(uint32_t,    slotIndex,         kAttrSerialize)
    // Empty (default) ⇒ this slot is OFF. Non-empty ⇒ bind / rebind.
    AY_PROPERTY(std::string, additiveClipPath,  kAttrSerialize)
    AY_PROPERTY(float,       additivePlayRate,  kAttrSerialize)
    AY_PROPERTY(bool,        looping,           kAttrSerialize)
    // Per-layer blend weight (P1.3 setBlendWeight per-slot generalisation).
    AY_PROPERTY(float,       blendWeight,       kAttrSerialize)
    // P1.4 per-slot cross-fade knobs (mirrors AnimationComponent's
    // legacy single-slot fields).
    AY_PROPERTY(bool,        syncToBase,        kAttrSerialize)
    AY_PROPERTY(bool,        refPoseCapture,    kAttrSerialize)
    AY_PROPERTY(float,       blendCurveFrom,    kAttrSerialize)
    AY_PROPERTY(float,       blendCurveTo,      kAttrSerialize)
    AY_PROPERTY(float,       blendCurveDuration,kAttrSerialize)
    AY_PROPERTY(uint8_t,     blendCurveEasing,  kAttrSerialize)

    AdditiveLayerSpec() {
        slotIndex = UINT32_MAX;     // sentinel — bridge will set to position
        additiveClipPath = "";      // empty = slot OFF
        additivePlayRate = 1.0f;
        looping = true;
        blendWeight = 1.0f;
        syncToBase = false;
        refPoseCapture = false;
        blendCurveFrom = 0.0f;
        blendCurveTo = 1.0f;
        blendCurveDuration = 0.0f;  // 0 = curve OFF
        blendCurveEasing = 0;       // 0 = ayt::anim::BlendEasing::Linear
    }
};
#undef AY_CURRENT_CLASS
// AdditiveLayerSpec TypeInfo is finalized in AYEntityReflection.cpp
// (must not live in this header — duplicate static finalizers across
// TUs corrupt the CRT debug heap; see AYEntityReflection.cpp banner).

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
    // AnimationPlayer::setBlendWeight by AnimationSystem::onUpdate. The
    // setter saturates to [0, 1]; default 1.0f means "additive tracks (if
    // any) are fully on". Pre-P1.2 builds leave the field at the ctor
    // default (1.0f) — bit-identical behavior for clips with no Additive
    // tracks. P1.6: field name retained for serializer round-trip compat
    // with P1.2/P1.3/P1.4 authored scenes; bridge push uses the canonical
    // setBlendWeight (the deprecated setAdditiveWeight inline-forward
    // wrapper was removed in P1.6).
    AY_PROPERTY(float,       additiveWeight, kAttrSerialize)
    // Phase 1.3 (P1.3) — Additive Layer 2 (Cross-Fade) bridge fields.
    // additiveClipPath empty (default) ⇒ no additive layer is bound; the
    // per-frame push degrades to setAdditiveSource(nullptr). P1.2's
    // additiveWeight becomes blendWeight in the canonical P1.3 API
    // (see AnimationPlayer::setBlendWeight), but we keep the field name
    // for serializer round-trip compat — AnimationSystem forwards it
    // through setBlendWeight directly (the canonical P1.3 API; the
    // deprecated setAdditiveWeight inline-forward wrapper was removed
    // in P1.6).
    //
    // UPGRADE-HOOK(P1.4): additivePlayRate + additiveLooping →
    //   syncToBase option (single bool on the player).
    // UPGRADE-HOOK(P1.5 → resolved): blendWeight + additiveWeight +
    //   scalar additiveClipPath all collapse into the additiveLayers[]
    //   vector field below. The legacy scalars are preserved for
    //   serializer round-trip — when additiveLayers.empty() the bridge
    //   reads the scalars and pushes slot[0].
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

    // P1.5 — Multi-source stack. additiveLayers[i] binds to slot[i]
    // on AnimationPlayer (per AdditiveLayerSpec::slotIndex — position
    // in the vector if slotIndex is UINT32_MAX). When non-empty the
    // bridge runs the per-slot push loop and ignores the scalar
    // fields above; when empty the bridge falls back to the legacy
    // single-slot scalar path (preserving P1.3/P1.4 authored scenes).
    //
    // Max 8 entries — hard cap matches AnimationPlayer's kMaxAdditiveSlots.
    // The bridge silently drops entries beyond index 7 (defensive — never
    // crash on a malformed component).
    AY_PROPERTY(std::vector<AdditiveLayerSpec>, additiveLayers, kAttrSerialize)

    // P2.2 (2026-08-03) — Skeleton Mask resource path. Non-empty ⇒ the
    // bridge loads an ISkeletonMask via ResourceManager and binds it to
    // the AnimationPlayer each tick (rebind detection on path). Empty ⇒
    // no mask — legacy behavior bit-identical to P2.1.
    //
    // Per-bone mask is orthogonal to P1.5 trackWeights (per-slot
    // per-track): a clip authored against the skeleton ("upper body
    // only") can be applied uniformly across any additive layer without
    // the host having to know the clip's track layout.
    //
    // The .aymask loader is deferred per §4.2.1 — until that ships,
    // ResourceManager::load<ISkeletonMask>(path) returns nullptr and
    // the bridge degrades fail-soft to "no mask" (one warn per path, no
    // retry storm — see _lastAppliedMaskPath latch in AYAnimationSystem).
    AY_PROPERTY(std::string, maskPath, kAttrSerialize)

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
        // additiveLayers defaults to empty → bridge takes legacy single-slot path.
        maskPath = "";              // P2.2 — empty = no mask
    }

    bool isValid() const { return !clipPath.empty(); }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity