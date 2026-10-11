#pragma once
// Private immutable facade binding; never a dynamic post-install injection.
#include <AYEntity/DeterministicCharacterRespawn2D.h>

namespace ayt::entity::detail {
bool validRespawnConfig(const DetCharacterRespawn2DConfig& config);
DetTypedStateSchema respawnCharacterStateSchema(const DetCharacterController2DConfig& config);
DetTypedStateSchema respawnCharacterPolicySchema(const DetCharacterController2DConfig& config);
DetTypedStateSchema respawnStateSchema(const DetCharacterRespawn2DConfig& config);
DetTypedStateSchema respawnPolicySchema(const DetCharacterRespawn2DConfig& config);
bool registerRespawnState(DeterministicSession& session,const DetCharacterRespawn2DConfig& config);
void respawnPolicyMatches(DetTickContext& context,const DetCharacterRespawn2DConfig& config);
const DetRespawnSpawn2D* respawnSpawn(const DetCharacterRespawn2DConfig& config,std::uint32_t id);
/// Collision-owned private result. Applied remains true only until character post.
void resolveRespawn(DetTickContext& context,SimEntityId actor,const DetCharacterRespawn2DConfig& config,
    std::uint32_t spawn,DetRespawnResult2D result);
/// Post consumes an accepted collision commit; this never writes a pose.
bool consumeRespawn(DetTickContext& context,SimEntityId actor,const DetCharacterRespawn2DConfig& config);
} // namespace ayt::entity::detail
