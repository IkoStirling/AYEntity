#include "AYResourceAnimationAdapter.h"

// AYResource runtime contracts (interface-only — no assetsImpl/* deps here).
#include "IAYAnimation.h"
#include "IAYSkeleton.h"
#include "AYResourceManager.h"

#include <cstdio>

namespace ayt::entity::adapter
{

namespace
{

ayt::anim::TrackType mapTrackType(ayt::resource::AnimTrackType t)
{
    using S = ayt::resource::AnimTrackType;
    using D = ayt::anim::TrackType;
    switch (t) {
        case S::Vector3:    return D::Vector3;
        case S::Quaternion: return D::Quaternion;
        case S::Float:      return D::Float;
    }
    return D::Vector3;
}

} // namespace

bool loadSkeleton(const std::string& path, ayt::anim::Skeleton& out)
{
    auto skel = ayt::resource::ResourceManager::instance()
                    .load<ayt::resource::ISkeleton>(path);
    if (!skel || skel->getBoneCount() == 0) {
        std::fprintf(stderr,
                     "[AYResourceAnimationAdapter] loadSkeleton('%s') failed\n",
                     path.c_str());
        return false;
    }

    const size_t n = skel->getBoneCount();

    // Phase 1 AN-03: bone metadata + parallel local TRS arrays. The
    // AYAnimation Skeleton stores `Bone` (name, parentIndex,
    // inverseBindMatrix) separately from the local TRS parallel arrays.
    // AYResource's ISkeleton unifies all of those fields into one
    // Bone struct, so we copy in two passes (first addBone, then
    // setRestPoses).
    for (size_t i = 0; i < n; ++i) {
        ayt::anim::Bone b;
        const ayt::resource::Bone& rb = skel->getBones()[i];
        b.name             = rb.name;
        b.parentIndex      = rb.parentIndex;
        b.inverseBindMatrix = rb.inverseBindMatrix;
        out.addBone(b);
    }

    // Parallel arrays: positions / rotations / scales all share length n.
    out.setRestPoses(skel->getLocalPositions(),
                     skel->getLocalRotations(),
                     skel->getLocalScales());

    return true;
}

bool loadAnimation(const std::string& path, ayt::anim::Animation& out)
{
    auto anim = ayt::resource::ResourceManager::instance()
                    .load<ayt::resource::IAnimation>(path);
    if (!anim) {
        std::fprintf(stderr,
                     "[AYResourceAnimationAdapter] loadAnimation('%s') failed\n",
                     path.c_str());
        return false;
    }

    out.setName(anim->getName());
    // Phase 1 AN-03: AYResource stores duration in *seconds already*
    // (FBX converter converts tick-space to seconds before writing the
    // .ayanm file). `getTicksPerSecond()` is still meaningful for the
    // legacy `times` array which is *raw ticks* — see below.
    out.setDuration(anim->getDuration());
    out.setTicksPerSecond(anim->getTicksPerSecond());

    // Time-unit conversion: AYResource's AnimTrack.times are raw
    // FBX ticks; AYAnimation's KeyframeTrack.times must be in
    // seconds (per the AnimationPlayer contract). Divide by tps.
    const float tps = anim->getTicksPerSecond();
    const float invTps = (tps > 0.0f) ? (1.0f / tps) : 1.0f;

    const uint32_t trackCount = anim->getTrackCount();
    for (uint32_t ti = 0; ti < trackCount; ++ti) {
        ayt::anim::KeyframeTrack track;
        track.nodeName = anim->getTrackNodeName(ti);
        track.property = anim->getTrackProperty(ti);
        track.type     = mapTrackType(anim->getTrackType(ti));

        const uint32_t keyCount = anim->getTrackKeyframeCount(ti);
        track.times.assign(keyCount, 0.0f);
        const float* rawTimes = anim->getTrackTimes(ti);
        for (uint32_t k = 0; k < keyCount; ++k) {
            track.times[k] = rawTimes[k] * invTps;
        }

        // Values: flat float buffer of N*strides where stride is
        // 3/4/1 for Vector3/Quaternion/Float. nodeName/property were
        // already captured above via the IAnimation interface accessors.
        const uint32_t valueCount = (track.type == ayt::anim::TrackType::Vector3)    ? keyCount * 3
                                  : (track.type == ayt::anim::TrackType::Quaternion) ? keyCount * 4
                                                                                       : keyCount;
        track.values.assign(valueCount, 0.0f);

        switch (track.type) {
            case ayt::anim::TrackType::Vector3: {
                const ayt::math::FVector3* v3 = anim->getTrackVector3Values(ti);
                for (uint32_t k = 0; k < keyCount; ++k) {
                    track.values[k * 3 + 0] = v3[k].x;
                    track.values[k * 3 + 1] = v3[k].y;
                    track.values[k * 3 + 2] = v3[k].z;
                }
                break;
            }
            case ayt::anim::TrackType::Quaternion: {
                const ayt::math::FQuaternion* q4 = anim->getTrackQuaternionValues(ti);
                for (uint32_t k = 0; k < keyCount; ++k) {
                    track.values[k * 4 + 0] = q4[k].x;
                    track.values[k * 4 + 1] = q4[k].y;
                    track.values[k * 4 + 2] = q4[k].z;
                    track.values[k * 4 + 3] = q4[k].w;
                }
                break;
            }
            case ayt::anim::TrackType::Float: {
                const float* fv = anim->getTrackFloatValues(ti);
                for (uint32_t k = 0; k < keyCount; ++k) {
                    track.values[k] = fv[k];
                }
                break;
            }
        }

        out.addTrack(std::move(track));
    }

    return true;
}

} // namespace ayt::entity::adapter