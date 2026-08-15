#pragma once
// AYEntity/components/AYEntity/components/AYEntity/components/AYEntity/components/SkeletonComponent.h — Phase 1 E-01: ECS bridge to AYAnimation's
// Skeleton + AnimationPlayer. Owns the playback state and the per-bone
// skin matrices that the renderer uploads to the Skeleton UBO.
//
// P0 (2026-07-26): skeleton field is now a concrete
// ayt::resource::Skeleton (not the deleted ayt::anim::Skeleton). The
// adapter that previously copied fields from ISkeleton -> ayt::anim::Skeleton
// is gone; AnimationPlayer consumes ISkeleton directly via setSkeleton.
//
// P1.7 (2026-07-27): skeleton field is now
// std::shared_ptr<const ayt::resource::ISkeleton> — we retain a
// strong reference into ResourceManager so the ISkeleton asset stays
// alive while at least one entity references it. N entities sharing
// the same skeletonPath all share one ISkeleton instance (the
// ResourceManager-cache + this component's shared_ptr form a single
// strong-reference graph). The per-entity by-value `Skeleton` copy
// loop in AnimationSystem::onUpdate is gone.

#include <AYEntity/IEntity.h>
#include <AYAnimation/AnimationPlayer.h>
#include <AYMath/MathTypes.h>

#include <AYResource/assetsDefs/ISkeleton.h>
#include <AYResource/assetsImpl/Skeleton.h>   // P1.7 — concrete Skeleton type for shared_ptr field

#include <cstdint>
#include <memory>
#include <string>

namespace ayt::entity
{

#define AY_CURRENT_CLASS SkeletonComponent
struct SkeletonComponent : public IComponent {
    const char* getName() const override { return "SkeletonComponent"; }

    // Bumped when the in-memory layout of this component changes
    // (e.g. AnimationPlayer went from embedded-by-value to unique_ptr).
    // AnimationSystem refuses to touch a component whose magic doesn't
    // match — catches stale .obj ABI mismatches that otherwise AV inside
    // AnimationPlayer::setSkeleton with a near-null this (write @ 0x20).
    static constexpr uint32_t kLayoutMagic = 0x534B4333; // 'SKC3'
    uint32_t layoutMagic = kLayoutMagic;

    // Asset reference. Declared via AY_PROPERTY (which expands to
    // `Type name;` + serializer metadata). Don't redeclare.
    AY_PROPERTY(std::string, skeletonPath, kAttrSerialize)

    // P1.7 — strong reference to the shared ISkeleton asset. The
    // shared_ptr's source of truth is ResourceManager::instance()
    // (which owns the cache + LRU); this component just pins the
    // skeleton so it cannot be evicted while in use. Multiple
    // entities with the same skeletonPath point to the same
    // ISkeleton instance (same .get() address).
    //
    // Held as shared_ptr<Skeleton> (mutable) rather than
    // shared_ptr<const ISkeleton> so tests can build a skeleton
    // programmatically via setBoneCount / setBone before pinning it
    // here. The conversion shared_ptr<Skeleton> → shared_ptr<const
    // ISkeleton> is implicit when handing to AnimationPlayer.
    std::shared_ptr<ayt::resource::Skeleton> skeleton;

    // Heap-allocated via AnimationPlayer::create() + AnimationPlayerDeleter
    // (deletion stays in AYAnimation.cpp — avoids LNK2005 on ~AnimationPlayer).
    std::unique_ptr<ayt::anim::AnimationPlayer, ayt::anim::AnimationPlayerDeleter> player;

    // Heap-allocated skin matrices (world * inverseBind), one entry per
    // bone. The renderer reads this array via `getBoneSkinMatrices()`
    // and uploads it to the SkinnedLit's `Skeleton` UBO each frame.
    // P1.7 — sizing still uses jointCount (a uint32_t copied from the
    // shared skeleton's bone count) so the renderer contract is
    // unchanged.
    ayt::math::Float4x4* skinMatrices = nullptr;
    // P2.1 — BlendSpace-base skin matrices. Written by BlendSpaceSystem
    // (priority 430) when the entity has a BlendSpaceComponent; read by
    // AnimationSystem (priority 450) as the authoritative base when
    // non-null. Orthogonal-fields model: both systems can drive the
    // entity without overwriting each other, and additive layers
    // (AnimationComponent.additiveLayers[]) ride on top of the
    // BlendSpace base via the unchanged AnimationSystem phase 1b.
    // Lifecycle mirrors skinMatrices (allocated by the relevant system
    // on first tick; freed in this dtor).
    ayt::math::Float4x4* skinMatricesBlendSpace = nullptr;
    uint32_t             jointCount   = 0;
    bool                 loaded       = false;

    bool isValid() const { return loaded && jointCount > 0; }

    // Out-of-line: unique_ptr<AnimationPlayer> create/destroy must not
    // be instantiated in every TU that includes this header (MSVC
    // LNK2005 on AnimationPlayer ctor/dtor COMDATs).
    SkeletonComponent();
    ~SkeletonComponent();
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity