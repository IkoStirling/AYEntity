#pragma once
#include <cstdint>
namespace ayt::render { struct SceneLights; }
namespace ayt::entity {
class World;
struct SceneLightingBuildResult {
    uint32_t authored=0, disabled=0, invalid=0, emitted=0, overflow=0;
};
/// Renderer integration adapter, not a gameplay lighting setter. Rebuilds output
/// from the current World at presentation alpha. At most eight valid enabled
/// lights, sorted by descending priority then persistent identity. Invalid
/// runtime values/poses are skipped; no pointers escape. Authored==0 lets the
/// Host retain its existing lighting; authored>0 plus emitted==0 explicitly disables direct lights (environment/emission remain).
SceneLightingBuildResult collectSceneLighting(World& world, render::SceneLights& output,
                                              float interpolationAlpha=1.0f);
} // namespace ayt::entity
