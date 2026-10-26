#pragma once

#include <AYEntity/ConnectedSpaces.h>
#include <AYRenderer/DeferredDecal.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ayt::entity {
class World;

struct SceneDecalCandidate {
    render::DeferredDecal decal;
    std::string albedoPath, normalPath;
};
struct SceneDecalBuildResult {
    std::uint32_t authored = 0, disabled = 0, invalid = 0;
    std::uint32_t emitted = 0, overflow = 0;
};

/// Extract at most 16 valid projectors in descending priority and stable
/// Entity identity order. A nonzero region filters candidates before budget
/// selection. Output is owned and contains no GPU texture handles; resolve
/// paths in the Renderer owner context before setDeferredDecals.
[[nodiscard]] SceneDecalBuildResult collectSceneDecals(
    World& world, std::vector<SceneDecalCandidate>& output,
    float interpolationAlpha = 1.0f, SpaceRegionId region = 0);

} // namespace ayt::entity
