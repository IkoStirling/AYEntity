#pragma once
// AYBlendSpaceSystem.h — P2.1 (2026-07-27). Per-frame driver for
// BlendSpace1D / BlendSpace2D trees attached to entities via
// BlendSpaceComponent.
//
// priority = 430 runs BEFORE AnimationSystem (priority 450). The
// system writes per-bone skin matrices into
// SkeletonComponent::skinMatricesBlendSpace; AnimationSystem's
// memcpy picks that field as the authoritative base when non-null,
// so a single entity can carry both BlendSpaceComponent (base) and
// AnimationComponent.additiveLayers[] (additive on top) without
// either component fighting the other for the skin matrix output.
//
// Pure consumer of the BlendSpace class — does NOT touch
// AnimationPlayer, ISkeleton / IAnimation directly. The BlendSpace
// owns its N internal AnimationPlayers + AssetBoneCache wiring;
// this system only feeds sample input + dt and copies the
// resulting matrices out.

#include <IAYEntity.h>

#include <ayanimation/BlendSpace.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::entity
{

class Entity;

class BlendSpaceSystem : public ISystem {
public:
    const char* getName() const override { return "BlendSpaceSystem"; }
    void onUpdate(float dt) override;

    static constexpr int kPriority = 430;

private:
    // Per-entity BlendSpace + per-clip caches. The lazy allocation
    // mirrors AnimationSystem's _clipCache pattern.
    struct EntityState {
        // Hold BOTH spaces so a 1D↔2D toggle doesn't require a
        // re-allocation. The system consults BlendSpaceComponent::is2D
        // each frame and dispatches to the active one.
        ayt::anim::BlendSpace1D bs1D;
        ayt::anim::BlendSpace2D bs2D;
        bool                    is2D = false;
        // Per-entity scratch — sized to the entity's skeleton bone
        // count by resizeTRS() the first time we observe it.
        std::vector<float> scratchPos;
        std::vector<float> scratchRot;
        std::vector<float> scratchScl;
        // Last-applied entries[] for rebind detection (skip the
        // per-frame `addSamplePoint` work when nothing changed).
        std::vector<std::string> lastPaths;
        std::vector<ayt::math::FVector2> lastPositions;
        bool                       lastIs2D = false;
    };
    std::unordered_map<const Entity*, EntityState> _entityStates;

    // Shared clip cache (path → IAnimation) so N entities sharing a
    // sample-point clip path only parse once. Mirrors AnimationSystem.
    std::unordered_map<std::string,
                       std::shared_ptr<ayt::resource::IAnimation>> _clipCache;
};

// Manual registration entry point (mirrors AnimationSystem). The
// AY_SYSTEM macro auto-registers on first TU initialization; this
// manual wrapper is here for tests / scripts that need explicit
// control over registration order after World::initialize().
void registerBlendSpaceSystem();

} // namespace ayt::entity