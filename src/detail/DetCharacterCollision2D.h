#pragma once
// design reference: AYEntity/design.md Stage26 static step-up / sole collision motion owner.
#include <AYEntity/DeterministicCollision2D.h>

namespace ayt::entity::detail {
/// Private facade binding. The controller owns pre requests and post consumption;
/// callers must not install a second collision owner or retain protocol fields.
/// Optional immutable controller schema binding reads Enabled10/Support30/
/// AppliedCarry80. Only continuous carry of a stopped support is cancelled;
/// the accepted carry is written for the facade post pass, never inherited own V.
bool installDetCharacterCollision2D(DeterministicSession& session,DetCollision2DConfig config,
    std::uint32_t controllerStateSchema=0,math::DetFloat32 maxSlopeRisePerRun=math::DetFloat32::fromInt(1),
    const DetCharacterRespawn2DConfig* respawn=nullptr);
}
