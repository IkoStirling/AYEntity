#pragma once
// design reference: AYEntity/design.md Stage26 static step-up / sole collision motion owner.
#include <AYEntity/DeterministicCollision2D.h>

namespace ayt::entity::detail {
/// Private facade binding. The controller owns pre requests and post consumption;
/// callers must not install a second collision owner or retain protocol fields.
bool installDetCharacterCollision2D(DeterministicSession& session,DetCollision2DConfig config);
}
