#pragma once
#include <AYEntity/DeterministicCharacterController2D.h>
#include <utility>

namespace ayt::entity::detail {
// Reuse the controller's actual profile selector and unchanged fingerprints.
std::pair<std::uint32_t,std::uint64_t> characterControllerIdentity2D(const DetCharacterController2DConfig& config);
// Existing pure support/standing-state validator, shared by internal facades.
// This validates compatible state and policy, never execution history.
void validateCharacterCheckpoint2D(const DetSessionCheckpoint& checkpoint,const DetCharacterController2DConfig& config);
}
