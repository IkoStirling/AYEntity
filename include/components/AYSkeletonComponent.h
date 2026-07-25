#pragma once
// AYSkeletonComponent.h — Phase 1 E-01: ECS bridge to AYAnimation's
// Skeleton + AnimationPlayer. Owns the playback state and the per-bone
// skin matrices that the renderer uploads to the Skeleton UBO.
//
// P0 (2026-07-26): skeleton field is now a concrete
// ayt::resource::Skeleton (not the deleted ayt::anim::Skeleton). The
// adapter that previously copied fields from ISkeleton -> ayt::anim::Skeleton
// is gone; AnimationPlayer consumes ISkeleton directly via setSkeleton.

#include <IAYEntity.h>
#include <ayanimation/AnimationPlayer.h>
#include <aymath/MathTypes.h>

#include <assetsImpl/AYSkeleton.h>

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

    // Loaded runtime data. `_skeleton` and `_player` are populated by
    // the adapter; callers should not write to them directly.
    ayt::resource::Skeleton    skeleton;
    ayt::anim::AnimationPlayer player;

    // Heap-allocated skin matrices (world * inverseBind), one entry per
    // bone. The renderer reads this array via `getBoneSkinMatrices()`
    // and uploads it to the SkinnedLit's `Skeleton` UBO each frame.
    ayt::math::Float4x4* skinMatrices = nullptr;
    uint32_t             jointCount   = 0;
    bool                 loaded       = false;

    bool isValid() const { return loaded && jointCount > 0; }

    SkeletonComponent() {
        // AY_PROPERTY emits `Type name;` with no default; ctor
        // assignment is required to leave it in a defined state.
        skeletonPath.clear();
    }

    // Cleanup hook called by EntitySubSystem when the component is
    // detached. Releases heap-allocated skin matrices.
    ~SkeletonComponent() {
        delete[] skinMatrices;
        skinMatrices = nullptr;
        jointCount = 0;
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity