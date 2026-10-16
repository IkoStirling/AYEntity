#pragma once
#include <memory>
#include <vector>
namespace ayt::entity {
struct DetCollision2DConfig;struct DetCharacterController2DConfig;struct DetPathMotion2DConfig;
struct DetInteraction2DConfig;struct DetCharacterRespawn2DConfig;struct DetCharacterRespawnFlow2DConfig;
/// Diagnostic policy copies retained only after successful installation. Not canonical state.
/// Owned by Session; owner-thread readers must use a checkpoint from that same Session.
struct DetDebugPolicies2D {
    std::vector<std::shared_ptr<const DetCollision2DConfig>> collisions;
    std::vector<std::shared_ptr<const DetCharacterController2DConfig>> characters;
    std::vector<std::shared_ptr<const DetPathMotion2DConfig>> paths;
    std::vector<std::shared_ptr<const DetInteraction2DConfig>> interactions;
    std::vector<std::shared_ptr<const DetCharacterRespawn2DConfig>> respawns;
    std::vector<std::shared_ptr<const DetCharacterRespawnFlow2DConfig>> respawnFlows;
};
}
