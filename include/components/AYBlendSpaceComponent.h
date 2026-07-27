#pragma once
// AYBlendSpaceComponent.h — P2.1 (2026-07-27). ECS handle for the
// BlendSpace1D / BlendSpace2D tree an entity should drive.
//
// Orthogonal to AnimationComponent (the existing AN-03 base-clip
// component). When an entity carries BOTH, AnimationSystem's memcpy
// picks the BlendSpace-computed skin matrices as the authoritative
// base; the AnimationComponent's additiveLayers stack on top of
// that base. See AYAnimationSystem.cpp memcpy-pick-non-null edit.

#include <IAYEntity.h>

#include <aymath/MathTypes.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ayt::entity
{

// P2.1 — one sample point in a BlendSpace tree. Spec format
// mirrors the editor's "Add Sample Point" panel. For a 1D blend
// only samplePosition.x is consulted (y is ignored). The 2D form
// uses both axes.
//
// blendSpaceIndex disambiguates entries when the host stacks a 1D
// and a 2D BlendSpace on the same entity (out-of-scope for P2.1
// but the field is reserved here for forward compat).
#define AY_CURRENT_CLASS BlendSpaceEntry
struct BlendSpaceEntry {
    AY_PROPERTY(ayt::math::FVector2, samplePosition, kAttrSerialize)
    AY_PROPERTY(std::string,         clipPath,       kAttrSerialize)
    AY_PROPERTY(float,               playRate,       kAttrSerialize)
    AY_PROPERTY(bool,                looping,        kAttrSerialize)
    // P2.x reserved — 1D vs 2D axis binding hint. P2.1 always
    // treats entries as the BlendSpaceComponent's is2D.
    AY_PROPERTY(uint8_t,             blendSpaceIndex,kAttrSerialize)

    BlendSpaceEntry() {
        samplePosition   = ayt::math::FVector2(0.0f, 0.0f);
        clipPath.clear();
        playRate         = 1.0f;
        looping          = true;
        blendSpaceIndex  = 0;
    }
};
#undef AY_CURRENT_CLASS

// Register BlendSpaceEntry with the AYReflect registry so the
// vector<BlendSpaceEntry> field below can be embedded in a
// component (the AY_PROPERTY registration for vector<T> requires
// T's TypeInfo to exist first; see PropertyMacros.h
// tryRegisterVector — same pattern AdditiveLayerSpec uses).
AY_FINALIZE_REGISTRATION_METADATA(BlendSpaceEntry)

// P2.1 — orthogonal to AnimationComponent. Drives a BlendSpace1D
// (when is2D == false) or BlendSpace2D (when is2D == true). The
// BlendSpaceSystem (priority 430) writes skin matrices into
// SkeletonComponent.skinMatricesBlendSpace every frame; the
// AnimationSystem (priority 450) memcpy picks that field as the
// authoritative base when non-null.
//
// Additive layers are NOT carried here — AnimationComponent's
// additiveLayers[] is the single source of truth for additive
// tracks and the BlendSpaceSystem does not touch them. A
// character with locomotion BlendSpace + upper-body additive layer
// can compose both with no extra wiring: the BlendSpaceSystem
// writes the base pose into skinMatricesBlendSpace; the additive
// layers ride on top via the unchanged AnimationSystem phase 1b.
#define AY_CURRENT_CLASS BlendSpaceComponent
struct BlendSpaceComponent : public IComponent {
    const char* getName() const override { return "BlendSpaceComponent"; }

    // is2D == false → BlendSpace1D; is2D == true → BlendSpace2D.
    // Default false (1D) keeps authored scenes from P2.1 lean.
    AY_PROPERTY(bool,                       is2D,        kAttrSerialize)
    AY_PROPERTY(std::vector<BlendSpaceEntry>, entries,    kAttrSerialize)
    // Host writes the live sample here every frame (gameplay
    // code: speed, direction, aim-offset coords, etc.). For 1D
    // mode only sampleInput.x is consulted.
    AY_PROPERTY(ayt::math::FVector2,         sampleInput, kAttrSerialize)
    AY_PROPERTY(float,                       playRate,    kAttrSerialize)
    AY_PROPERTY(bool,                        looping,     kAttrSerialize)

    BlendSpaceComponent() {
        is2D        = false;
        sampleInput = ayt::math::FVector2(0.0f, 0.0f);
        playRate    = 1.0f;
        looping     = true;
        // entries defaults to empty → BlendSpaceComponent::isValid()
        // returns false → the system skips this entity.
    }

    // The bridge consults isValid() to decide whether to populate
    // skinMatricesBlendSpace. With < 1 entry there is nothing to
    // blend and the system falls through to the legacy single-clip
    // path (AnimationComponent.clipPath).
    bool isValid() const { return entries.size() >= 1; }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity