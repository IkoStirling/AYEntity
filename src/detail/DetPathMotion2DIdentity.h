#pragma once
#include <AYEntity/DeterministicPathMotion2D.h>
#include <stdexcept>
#include <utility>

namespace ayt::entity::detail {
// Internal compatibility metadata, not additional gameplay authority. The
// public factory owns the actual descriptor and validates the selected policy.
inline std::pair<std::uint32_t,std::uint64_t> pathMotionIdentity2D(const DetPathMotion2DConfig& c){
    const auto version=detPathMotionStateSchema2D(c).version;
    if(version!=(c.movingTrigger?kDetPathMotion2DTriggerProfileVersion:kDetPathMotion2DProfileVersion))
        throw std::invalid_argument("deterministic path identity: unsupported descriptor");
    return {version,c.movingTrigger?UINT64_C(0x44504d3244503032):UINT64_C(0x44504d3244503031)};
}
} // namespace ayt::entity::detail
