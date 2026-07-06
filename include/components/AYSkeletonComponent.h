#pragma once
// AYSkeletonComponent.h — Phase 1 E-01: ECS bridge to AYAnimation's
// Skeleton + AnimationPlayer. Owns the playback state and the per-bone
// skin matrices that the renderer uploads to the Skeleton UBO.

#include <IAYEntity.h>
#include <ayanimation/Animation.h>
#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/Skeleton.h>
#include <AYMathTypes.h>

#include <cstdint>
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
    ayt::anim::Skeleton       skeleton;
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

AY_FINALIZE_REGISTRATION_METADATA(SkeletonComponent)

} // namespace ayt::entity