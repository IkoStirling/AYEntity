#pragma once
// AYSkeletonComponent.h — Phase 1 E-01: ECS bridge to AYAnimation's
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

#include <IAYEntity.h>
#include <ayanimation/AnimationPlayer.h>
#include <aymath/MathTypes.h>

#include <assetsDefs/IAYSkeleton.h>
#include <assetsImpl/AYSkeleton.h>   // P1.7 — concrete Skeleton type for shared_ptr field

#include <cstdint>
#include <memory>
#include <string>

namespace ayt::entity
{

#define AY_CURRENT_CLASS SkeletonComponent
struct SkeletonComponent : public IComponent {
    const char* getName() const override { return "SkeletonComponent"; }

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

    // The AnimationPlayer instance that does the per-frame sampling
    // and emits bone-skin matrices. Lives in the component so the
    // renderer can pin its skin-matrices pointer without going
    // through World / System state. Reused across frames — never
    // re-instantiated.
    ayt::anim::AnimationPlayer player;

    // Heap-allocated skin matrices (world * inverseBind), one entry per
    // bone. The renderer reads this array via `getBoneSkinMatrices()`
    // and uploads it to the SkinnedLit's `Skeleton` UBO each frame.
    // P1.7 — sizing still uses jointCount (a uint32_t copied from the
    // shared skeleton's bone count) so the renderer contract is
    // unchanged.
    ayt::math::Float4x4* skinMatrices = nullptr;
    uint32_t             jointCount   = 0;
    bool                 loaded       = false;

    bool isValid() const { return loaded && jointCount > 0; }

    SkeletonComponent() {
        // AY_PROPERTY emits `Type name;` with no default; ctor
        // assignment is required to leave it in a defined state.
        skeletonPath.clear();
        skeleton.reset();
    }

    // Cleanup hook called by EntitySubSystem when the component is
    // detached. Releases heap-allocated skin matrices. The shared_ptr
    // `skeleton` releases automatically; if no other entity references
    // the ISkeleton asset, ResourceManager's LRU is free to evict it
    // on the next trimCache() call.
    ~SkeletonComponent() {
        delete[] skinMatrices;
        skinMatrices = nullptr;
        jointCount = 0;
        skeleton.reset();
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity