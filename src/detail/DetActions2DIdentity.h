#pragma once
#include <AYEntity/DeterministicActions2D.h>
#include <algorithm>
#include <utility>

namespace ayt::entity::detail {
// Selection depends only on complete authorized target paths. A collision
// trigger without a Path2 target does not select the moving-trigger authority.
inline std::pair<std::uint32_t,std::uint64_t> actionsIdentity2D(const DetActions2DConfig& c){
    if(std::any_of(c.targets.begin(),c.targets.end(),[](const auto& target){return target.path && target.path->movingTrigger;}))
        return {kDetActions2DMovingTriggerProfileVersion,UINT64_C(0x4441433244503033)};
    if(c.collision.characterRespawn)return {kDetActions2DRespawnProfileVersion,UINT64_C(0x4441433244503032)};
    return {kDetActions2DProfileVersion,UINT64_C(0x4441433244503031)};
}
} // namespace ayt::entity::detail
