// AYSkeletonComponent.cpp — out-of-line ctor/dtor. player uses
// AnimationPlayerDeleter so delete runs in AYAnimation.cpp only.

#include "AYEntity/components/SkeletonComponent.h"

namespace ayt::entity
{

SkeletonComponent::SkeletonComponent()
{
    // Stamp layoutMagic FIRST — spawnCharacterFromPaths refuses to
    // touch a component whose magic doesn't match (stale .obj ABI).
    layoutMagic = kLayoutMagic;
    // AY_PROPERTY emits `Type name;` with no default; leave strings /
    // shared_ptr in a defined state. Do NOT create AnimationPlayer here:
    // bind-pose (empty clipPath) never needs one, and eager create was
    // fighting the AnimationSystem lazy path.
    skeletonPath.clear();
    skeleton.reset();
    // Heap player is cheap; setSkeleton (bone TRS buffers) stays lazy in
    // AnimationSystem until a clipPath is present. Tests and editor code
    // call skel->player-> directly after addComponent — null player AVs.
    player = ayt::anim::AnimationPlayer::create();
    // P2.1 — BlendSpace base skin-matrix slot (orthogonal-fields model).
    skinMatricesBlendSpace = nullptr;
}

SkeletonComponent::~SkeletonComponent()
{
    delete[] skinMatrices;
    skinMatrices = nullptr;
    delete[] skinMatricesBlendSpace;
    skinMatricesBlendSpace = nullptr;
    jointCount = 0;
    skeleton.reset();
    player.reset();
}

} // namespace ayt::entity
